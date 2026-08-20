/*
 *  Module for AzerothCore by 0x0003 (https://github.com/0x0003)
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License as published
 *  by the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "Player.h"
#include "ScriptMgr.h"
#include "SpellAuras.h"
#include "UnitDefines.h"
#include "Config.h"

// ============================================================================
// Spell IDs - originals applied by HandleShapeshiftBoosts
// We reference these for removal/checking but DO NOT cast them directly.
// ============================================================================

// The core Tree of Life passive
static constexpr uint32 SPELL_TOL_PASSIVE_ORIGINAL = 34123;

// Improved Tree of Life ranks 1-3
static constexpr uint32 SPELL_IMP_TOL_1_ORIGINAL = 48535;
static constexpr uint32 SPELL_IMP_TOL_2_ORIGINAL = 48536;
static constexpr uint32 SPELL_IMP_TOL_3_ORIGINAL = 48537;

// Tree of Life Healing Boost
// NOTE: possibly not used?
static constexpr uint32 SPELL_TOL_HEALING_BOOST_ORIGINAL = 62111;

// ============================================================================
// Spell IDs - custom copies created by spell_dbc.sql
// These mirror the originals above but with unique IDs so they don't
// conflict with HandleShapeshiftBoosts.
// ============================================================================

// Tree of Life passive
static constexpr uint32 SPELL_FUTA_TOL_PASSIVE = 96961;

// Improved Tree of Life ranks 1-3
static constexpr uint32 SPELL_FUTA_IMP_TOL_1 = 96964;
static constexpr uint32 SPELL_FUTA_IMP_TOL_2 = 96965;
static constexpr uint32 SPELL_FUTA_IMP_TOL_3 = 96966;

// Master Shapeshifter - Tree of Life
static constexpr uint32 SPELL_FUTA_MS_TREE_RANK1 = 96962;
static constexpr uint32 SPELL_FUTA_MS_TREE_RANK2 = 96963;

// Tree of Life Healing Boost
static constexpr uint32 SPELL_FUTA_TOL_HEALING_BOOST = 96967;

// ============================================================================
// Talent detection
// ============================================================================

// Tree of Life talent - teaches spell 65139.
// Spell 33891 is the active shapeshift form, not the talent spell.
static constexpr uint32 TALENT_TOL_PASSIVE = 65139;

// Master Shapeshifter talent - teaches 48420 (rank 1) or 48421 (rank 2).
// The passive effect is 48422, granted by either rank.
static constexpr uint32 TALENT_MS_RANK1 = 48411;
static constexpr uint32 TALENT_MS_RANK2 = 48412;

// ============================================================================
// Configuration
// ============================================================================

static bool sEnableTreeOfLife;
static bool sEnableImprovedTreeOfLife;
static bool sEnableMasterShapeshifter;
static bool sEnableTreeOfLifeHealingBoost;
static float sTreeOfLifeMultiplier;
static float sMasterShapeshifterMultiplier;
static float sImpTolArmorMultiplier;
static float sImpTolSpiritToHealingBonusMultiplier;

static void LoadFutaConfig()
{
    sEnableTreeOfLife             = sConfigMgr->GetOption<bool>("Futa.EnableTreeOfLife", true);
    sEnableImprovedTreeOfLife     = sConfigMgr->GetOption<bool>("Futa.EnableImprovedTreeOfLife", true);
    sEnableMasterShapeshifter     = sConfigMgr->GetOption<bool>("Futa.EnableMasterShapeshifter", true);
    sEnableTreeOfLifeHealingBoost = sConfigMgr->GetOption<bool>("Futa.EnableTreeOfLifeHealingBoost", false);
    sTreeOfLifeMultiplier         = sConfigMgr->GetOption<float>("Futa.TreeOfLife.HealingReceivedMultiplier", 1.0f);
    sMasterShapeshifterMultiplier = sConfigMgr->GetOption<float>("Futa.MasterShapeshifter.HealingDoneMultiplier", 1.0f);
    sImpTolArmorMultiplier        = sConfigMgr->GetOption<float>("Futa.ImprovedTreeOfLife.ArmorMultiplier", 0.33f);
    sImpTolSpiritToHealingBonusMultiplier = sConfigMgr->GetOption<float>("Futa.ImprovedTreeOfLife.SpiritToHealingBonusMultiplier", 1.0f);
}

// Effective values for CastCustomSpell;
// DBC stores base as value-1, but CastCustomSpell uses the value directly.
static constexpr int32 TOL_PASSIVE_BASE      = 6;                // +6% healing received
static constexpr int32 MS_TREE_BASE[]        = { 2, 4 };         // +2%, +4% healing done
static constexpr int32 IMP_TOL_ARMOR_BASE[]  = { 67, 133, 200 }; // +67%, +133%, +200%
static constexpr int32 IMP_TOL_SPIRIT_BASE[] = { 5, 10, 15 };    // +5%, +10%, +15%

// Talent-granted spells (65139, 48411/48412, 48535-48537) live in the
// player's talent map (m_talents), not in m_spells - Player::HasSpell only
// reads m_spells, so it returns false even for a properly talented druid.
// Player::_addTalentAurasAndSpells just learns passive talent spells
// without inserting the talent spell itself into m_spells.
static bool HasSpellOrTalent(Player const* player, uint32 spellId)
{
    if (player->HasSpell(spellId))
        return true;

    // Only the active spec counts
    return player->HasTalent(spellId, player->GetActiveSpec());
}

static bool HasTreeOfLifeTalent(Player const* player)
{
    if (!player || player->getClass() != CLASS_DRUID)
        return false;
    return HasSpellOrTalent(player, TALENT_TOL_PASSIVE);
}

static bool HasMasterShapeshifterTalent(Player const* player)
{
    if (!player || player->getClass() != CLASS_DRUID)
        return false;
    return HasSpellOrTalent(player, TALENT_MS_RANK1) || HasSpellOrTalent(player, TALENT_MS_RANK2);
}

static bool HasImprovedTreeOfLifeTalent(Player const* player)
{
    if (!player || player->getClass() != CLASS_DRUID)
        return false;
    return HasSpellOrTalent(player, SPELL_IMP_TOL_3_ORIGINAL)
        || HasSpellOrTalent(player, SPELL_IMP_TOL_2_ORIGINAL)
        || HasSpellOrTalent(player, SPELL_IMP_TOL_1_ORIGINAL);
}

// ============================================================================
// Aura management
// ============================================================================

static void RemoveFutaAuras(Player* player)
{
    player->RemoveAurasDueToSpell(SPELL_FUTA_TOL_PASSIVE);
    player->RemoveAurasDueToSpell(SPELL_FUTA_IMP_TOL_1);
    player->RemoveAurasDueToSpell(SPELL_FUTA_IMP_TOL_2);
    player->RemoveAurasDueToSpell(SPELL_FUTA_IMP_TOL_3);
    player->RemoveAurasDueToSpell(SPELL_FUTA_MS_TREE_RANK1);
    player->RemoveAurasDueToSpell(SPELL_FUTA_MS_TREE_RANK2);
    player->RemoveAurasDueToSpell(SPELL_FUTA_TOL_HEALING_BOOST);
}

static void ApplyFutaAuras(Player* player)
{
    if (!HasTreeOfLifeTalent(player))
        return;

    // Don't apply if shapeshifted - OnUnitSetShapeshiftForm handles shift-out
    if (player->GetShapeshiftForm() != FORM_NONE)
        return;

    // Clear any stale ranks before (re-)applying
    RemoveFutaAuras(player);

    if (sEnableTreeOfLife)
    {
        int32 bp = int32(TOL_PASSIVE_BASE * sTreeOfLifeMultiplier);
        player->CastCustomSpell(player, SPELL_FUTA_TOL_PASSIVE, &bp, nullptr, nullptr, true);
    }

    if (sEnableTreeOfLifeHealingBoost)
        player->CastSpell(player, SPELL_FUTA_TOL_HEALING_BOOST, true);

    if (sEnableImprovedTreeOfLife && HasImprovedTreeOfLifeTalent(player))
    {
        // Determine highest known rank (0=rank1, 1=rank2, 2=rank3)
        uint32 rank = 0;
        if (HasSpellOrTalent(player, SPELL_IMP_TOL_3_ORIGINAL))
            rank = 2;
        else if (HasSpellOrTalent(player, SPELL_IMP_TOL_2_ORIGINAL))
            rank = 1;

        uint32 spellId = (rank == 2) ? SPELL_FUTA_IMP_TOL_3
                       : (rank == 1) ? SPELL_FUTA_IMP_TOL_2
                       :               SPELL_FUTA_IMP_TOL_1;

        // Cast with per-effect base point overrides:
        //   bp0 = armor bonus (Effect 1, aura 142)
        //   bp1 = spirit-to-bonus-healing (Effect 2, aura 175)
        //   bp2 = unused (nullptr = use DBC value)
        int32 armorValue  = int32(IMP_TOL_ARMOR_BASE[rank]   * sImpTolArmorMultiplier);
        int32 spiritValue = int32(IMP_TOL_SPIRIT_BASE[rank] * sImpTolSpiritToHealingBonusMultiplier);
        player->CastCustomSpell(player, spellId, &armorValue, &spiritValue, nullptr, true);
    }

    if (sEnableMasterShapeshifter && HasMasterShapeshifterTalent(player))
    {
        uint32 rank = HasSpellOrTalent(player, TALENT_MS_RANK2) ? 1 : 0;
        uint32 spellId = (rank == 1) ? SPELL_FUTA_MS_TREE_RANK2 : SPELL_FUTA_MS_TREE_RANK1;
        int32 bp = int32(MS_TREE_BASE[rank] * sMasterShapeshifterMultiplier);
        player->CastCustomSpell(player, spellId, &bp, nullptr, nullptr, true);
    }
}

// ============================================================================
// Scripts
// ============================================================================

class FutaPlayerScript : public PlayerScript
{
public:
    FutaPlayerScript() : PlayerScript("FutaPlayerScript", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_PLAYER_LEARN_TALENTS,
        PLAYERHOOK_ON_TALENTS_RESET,
        PLAYERHOOK_ON_AFTER_SPEC_SLOT_CHANGED,
    }) { }

    void OnPlayerLogin(Player* player) override
    {
        ApplyFutaAuras(player);
    }

    void OnPlayerLearnTalents(Player* player, uint32 /*talentId*/, uint32 /*talentRank*/, uint32 /*spellid*/) override
    {
        ApplyFutaAuras(player);
    }

    void OnPlayerTalentsReset(Player* player, bool /*noCost*/) override
    {
        RemoveFutaAuras(player);
    }

    void OnPlayerAfterSpecSlotChanged(Player* player, uint8 /*newSlot*/) override
    {
        RemoveFutaAuras(player);
        ApplyFutaAuras(player);
    }
};

class FutaUnitScript : public UnitScript
{
public:
    FutaUnitScript() : UnitScript("FutaUnitScript", true, {
        UNITHOOK_ON_UNIT_SET_SHAPESHIFT_FORM,
    }) { }

    void OnUnitSetShapeshiftForm(Unit* unit, uint8 form) override
    {
        if (unit->GetTypeId() != TYPEID_PLAYER)
            return;

        Player* player = unit->ToPlayer();
        if (player->getClass() != CLASS_DRUID)
            return;

        ShapeshiftForm newForm = ShapeshiftForm(form);

        if (newForm == FORM_NONE)
        {
            // Entering humanoid form
            if (HasTreeOfLifeTalent(player))
                ApplyFutaAuras(player);
        }
        else
        {
            // Entering any shapeshift form
            RemoveFutaAuras(player);
        }
    }
};

// ============================================================================
// Loader
// ============================================================================

void AddFutaScripts()
{
    LoadFutaConfig();
    new FutaPlayerScript();
    new FutaUnitScript();
}
