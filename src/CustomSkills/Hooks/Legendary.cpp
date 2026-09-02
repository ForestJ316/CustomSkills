#include "Legendary.h"

#include "CustomSkills/CustomSkillsManager.h"
#include "RE/Offset.h"

#include <xbyak/xbyak.h>

#pragma section(".jit", execute)

namespace CustomSkills
{
	void Legendary::WriteHooks()
	{
		LegendaryAvailablePatch();
		PlayerSkillsPatch();
		RefundPerksPatch();
	}

	void Legendary::LegendaryAvailablePatch()
	{
		auto hook = REL::Relocation<std::uintptr_t>(RE::Offset::IsLegendaryDifficultyAvailable);

		auto IsLegendaryAvailable = +[]()
		{
			if (*"iDifficultyLevelMax"_gs < 5) {
				return false;
			}

			if (CustomSkillsManager::IsOurMenuMode()) {
				const auto ui = RE::UI::GetSingleton();
				const auto statsMenu = ui->GetMenu<RE::StatsMenu>();
				const auto actorValue = static_cast<RE::ActorValue>(
					CUSTOM_SKILL_BASE_VALUE + statsMenu->selectedTree);

				if (const auto skill = CustomSkillsManager::GetCurrentSkill(actorValue)) {
					return skill->Legendary != nullptr;
				}
			}

			return true;
		};

		REL::safe_fill(hook.address(), REL::INT3, 0x10);
		util::write_14branch(hook.address(), IsLegendaryAvailable);
	}

	void Legendary::PlayerSkillsPatch()
	{
		auto hook = REL::Relocation<std::uintptr_t>(
			RE::Offset::LegendarySkillResetConfirmCallback::Run,
			0x20D);
		REL::make_pattern<"E8">().match_or_fail(hook.address());

		using MakeLegendary_t = void (RE::PlayerCharacter::PlayerSkills::*)(RE::ActorValue);
		static REL::Relocation<MakeLegendary_t> _MakeLegendary;

		auto MakeLegendary = +[](RE::PlayerCharacter::PlayerSkills* a_playerSkills,
								 RE::ActorValue a_actorValue)
		{
			if (const auto skill = CustomSkillsManager::GetCurrentSkill(a_actorValue)) {
				if (skill->Ratio) {
					skill->Ratio->value = 0.0f;
				}

				if (skill->Legendary) {
					skill->Legendary->value += 1;
				}
			}
			else {
				_MakeLegendary(a_playerSkills, a_actorValue);
			}
		};

		// TRAMPOLINE: 14
		auto& trampoline = SKSE::GetTrampoline();
		_MakeLegendary = trampoline.write_call<5>(hook.address(), MakeLegendary);
	}

	void Legendary::RefundPerksPatch()
	{
		auto hook = REL::Relocation<std::uintptr_t>(
			RE::Offset::BGSSkillPerkTreeNode::RefundPerks,
			0xED);

		REL::make_pattern<"44 00 B8 ?? ?? 00 00">().match_or_fail(hook.address());

		static auto ModifyPerkPoints = +[](std::uint8_t a_countDelta)
		{
			auto newCount = CustomSkillsManager::GetCurrentPerkPoints() + a_countDelta;
			if (newCount > 255) {
				newCount = 255;
			}

			CustomSkillsManager::SetCurrentPerkPoints(static_cast<std::uint8_t>(newCount));
		};

		__declspec(allocate(".jit")) alignas(
			16) static constinit auto buffer = util::jit_buffer<48>();

		struct Patch : Xbyak::CodeGenerator
		{
			Patch(std::uintptr_t a_funcAddr, std::uintptr_t a_retnAddr)
				: Xbyak::CodeGenerator(buffer.size(), buffer.data())
			{
				Xbyak::Label funcLbl;
				Xbyak::Label retnLbl;

				mov(cl, r15b);
				call(ptr[rip + funcLbl]);
				xor_(r8d, r8d);
				mov(rdx, r13);

				jmp(ptr[rip + retnLbl]);

				L(funcLbl);
				dq(a_funcAddr);

				L(retnLbl);
				dq(a_retnAddr);
			}
		};

		if (auto ctx = REL::safe_write_context(buffer.data(), buffer.size())) {
			auto patch = Patch(
				reinterpret_cast<std::uintptr_t>(ModifyPerkPoints),
				hook.address() + 0x7);
			patch.ready();
			assert(((patch.getSize() + 0xF) & ~0xF) == buffer.size());
		}

		// TRAMPOLINE: 8
		auto& trampoline = SKSE::GetTrampoline();
		REL::safe_fill(hook.address(), REL::NOP, 0x7);
		trampoline.write_branch<6>(hook.address(), buffer.data());
	}
}
