#include "SkillBooks.h"

#include "CustomSkills/CustomSkillBookReadRegSet.h"
#include "CustomSkills/CustomSkillsManager.h"
#include "RE/Offset.h"

#include <xbyak/xbyak.h>

#pragma section(".jit", execute)

namespace CustomSkills
{
	void SkillBooks::WriteHooks()
	{
		CustomSkillPatch();
	}

	static bool ReadSkillBook(RE::TESObjectBOOK* a_book)
	{
		if (a_book->IsRead())
			return false;

		for (const RE::BGSKeyword* const keyword :
			 std::span(a_book->keywords, a_book->numKeywords)) {

			if (!keyword)
				continue;

			const auto str = std::string_view(keyword->formEditorID);
			static constexpr auto prefix = "CustomSkillBook_"sv;
			if (str.size() <= prefix.size() ||
				::_strnicmp(str.data(), prefix.data(), prefix.size()) != 0) {
				continue;
			}

			const auto skill = CustomSkillsManager::FindSkill(str.substr(prefix.size()));
			if (!skill)
				continue;

			const auto player = RE::PlayerCharacter::GetSingleton();
			float count = 1.0f;
			RE::BGSEntryPoint::HandleEntryPoint(
				RE::BGSEntryPoint::ENTRY_POINT::kAdjustBookSkillPoints,
				player,
				&count);

			CustomSkillBookReadRegSet::Get()
				->SendEvent(a_book, skill->ID, static_cast<std::int32_t>(count));
			return true;
		}

		return false;
	}

	void SkillBooks::CustomSkillPatch()
	{
		auto hook = REL::Relocation<std::uintptr_t>(RE::Offset::TESObjectBOOK::Read, 0x103);
		REL::make_pattern<"41 BD FF FF FF FF">().match_or_fail(hook.address());

		__declspec(allocate(".jit")) alignas(
			16) static constinit auto buffer = util::jit_buffer<64>();

		struct Patch : Xbyak::CodeGenerator
		{
			Patch(std::uintptr_t a_hookAddr, std::uintptr_t a_funcAddr)
				: Xbyak::CodeGenerator(buffer.size(), buffer.data())
			{
				Xbyak::Label funcLbl;
				Xbyak::Label learnedSkill;

				mov(r13d, 0xFFFFFFFF);
				mov(rcx, r15);
				call(ptr[rip + funcLbl]);
				cmp(al, 0);
				jnz(learnedSkill, T_SHORT);
				movzx(ecx, byte[r15 + 0x110]);

				jmp(ptr[rip]);
				dq(a_hookAddr + 0x6);

				L(learnedSkill);
				jmp(ptr[rip]);
				dq(a_hookAddr + 0x70);

				L(funcLbl);
				dq(a_funcAddr);
			}
		};

		if (auto ctx = REL::safe_write_context(buffer.data(), buffer.size())) {
			auto patch = Patch(hook.address(), reinterpret_cast<std::uintptr_t>(&ReadSkillBook));
			patch.ready();
			assert(((patch.getSize() + 0xF) & ~0xF) == buffer.size());
		}

		auto& trampoline = SKSE::GetTrampoline();
		trampoline.write_branch<6>(hook.address(), buffer.data());
	}
}
