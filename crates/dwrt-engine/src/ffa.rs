//! Pure FFA policy tests and helpers.
//!
//! These helpers intentionally model DWRT's *virtual* target policy. They do
//! not rewrite Deadlock engine classes, vtables, teams, networking, or game
//! rules. Live hooks/probes must prove where these virtual decisions can be
//! applied safely.

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TeamRelation {
    SameTeam,
    EnemyTeam,
    NeutralTarget,
    Unknown,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CombatRelation {
    Friendly,
    Enemy,
    Neutral,
    Unknown,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum UnitKind {
    Hero,
    Trooper,
    Boss,
    Building,
    Prop,
    Minion,
    GoldOrb,
    Trophy,
    Neutral,
    Zipline,
    BreakableProp,
    AbilityTrigger,
    Unknown,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TargetMask(u32);

impl TargetMask {
    pub const NONE: Self = Self(0);
    pub const HERO_FRIENDLY: Self = Self(0x00001);
    pub const TROOPER_FRIENDLY: Self = Self(0x00002);
    pub const BOSS_FRIENDLY: Self = Self(0x00004);
    pub const BUILDING_FRIENDLY: Self = Self(0x00008);
    pub const PROP_FRIENDLY: Self = Self(0x00010);
    pub const MINION_FRIENDLY: Self = Self(0x00020);
    pub const GOLD_ORBS_FRIENDLY: Self = Self(0x00040);
    pub const TROPHY_FRIENDLY: Self = Self(0x00080);
    pub const HERO_ENEMY: Self = Self(0x00100);
    pub const TROOPER_ENEMY: Self = Self(0x00200);
    pub const BOSS_ENEMY: Self = Self(0x00400);
    pub const BUILDING_ENEMY: Self = Self(0x00800);
    pub const PROP_ENEMY: Self = Self(0x01000);
    pub const MINION_ENEMY: Self = Self(0x02000);
    pub const GOLD_ORBS_ENEMY: Self = Self(0x04000);
    pub const TROPHY_ENEMY: Self = Self(0x08000);
    pub const NEUTRAL: Self = Self(0x10000);
    pub const ZIPLINE: Self = Self(0x20000);
    pub const BREAKABLE_PROP: Self = Self(0x40000);
    pub const DYNAMIC_PROP: Self = Self(0x40000);
    pub const ABILITY_TRIGGER: Self = Self(0x80000);
    pub const HERO: Self = Self(Self::HERO_FRIENDLY.0 | Self::HERO_ENEMY.0);
    pub const BOSS: Self = Self(Self::BOSS_FRIENDLY.0 | Self::BOSS_ENEMY.0);
    pub const BUILDING: Self = Self(Self::BUILDING_FRIENDLY.0 | Self::BUILDING_ENEMY.0);
    pub const ALL_FRIENDLY: Self = Self(0x0003f);
    pub const ALL_ENEMY: Self = Self(0x13f00);
    pub const ALL: Self = Self(0x13f3f);

    #[must_use]
    pub const fn bits(self) -> u32 {
        self.0
    }

    #[must_use]
    pub const fn union(self, other: Self) -> Self {
        Self(self.0 | other.0)
    }

    #[must_use]
    pub const fn intersects(self, other: Self) -> bool {
        (self.0 & other.0) != 0
    }

    #[must_use]
    pub const fn contains(self, other: Self) -> bool {
        (self.0 & other.0) == other.0
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TargetContext {
    pub source_team: Option<u8>,
    pub target_team: Option<u8>,
    pub target_kind: UnitKind,
    pub target_is_player: bool,
}

impl TargetContext {
    #[must_use]
    pub const fn new(
        source_team: Option<u8>,
        target_team: Option<u8>,
        target_kind: UnitKind,
        target_is_player: bool,
    ) -> Self {
        Self {
            source_team,
            target_team,
            target_kind,
            target_is_player,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct FfaTargetPolicy {
    pub allow_same_team_players: bool,
    pub allow_same_team_objectives: bool,
    pub player_boss_overlay: bool,
}

impl FfaTargetPolicy {
    #[must_use]
    pub const fn disabled() -> Self {
        Self {
            allow_same_team_players: false,
            allow_same_team_objectives: false,
            player_boss_overlay: false,
        }
    }

    #[must_use]
    pub const fn players_only() -> Self {
        Self {
            allow_same_team_players: true,
            allow_same_team_objectives: false,
            player_boss_overlay: false,
        }
    }

    #[must_use]
    pub const fn players_and_objectives() -> Self {
        Self {
            allow_same_team_players: true,
            allow_same_team_objectives: true,
            player_boss_overlay: false,
        }
    }

    #[must_use]
    pub const fn with_player_boss_overlay(mut self) -> Self {
        self.player_boss_overlay = true;
        self
    }
}

impl Default for FfaTargetPolicy {
    fn default() -> Self {
        Self::disabled()
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum EngineTeamPlan {
    KeepEngineTeams,
    UniqueTeamPerPlayer,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TeamPlanError {
    UniqueEngineTeamsBreakServerAssumptions,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct LayerDecision {
    pub target_filter_allows: bool,
    pub damage_policy_allows: bool,
}

#[must_use]
pub const fn team_relation(context: TargetContext) -> TeamRelation {
    if matches!(context.target_kind, UnitKind::Neutral) {
        return TeamRelation::NeutralTarget;
    }
    match (context.source_team, context.target_team) {
        (Some(source), Some(target)) if source == target => TeamRelation::SameTeam,
        (Some(_), Some(_)) => TeamRelation::EnemyTeam,
        _ => TeamRelation::Unknown,
    }
}

#[must_use]
pub const fn virtual_combat_relation(
    context: TargetContext,
    policy: FfaTargetPolicy,
) -> CombatRelation {
    match team_relation(context) {
        TeamRelation::EnemyTeam => CombatRelation::Enemy,
        TeamRelation::NeutralTarget => CombatRelation::Neutral,
        TeamRelation::Unknown => CombatRelation::Unknown,
        TeamRelation::SameTeam => {
            if (context.target_is_player && policy.allow_same_team_players)
                || (is_objective(context.target_kind) && policy.allow_same_team_objectives)
            {
                CombatRelation::Enemy
            } else {
                CombatRelation::Friendly
            }
        }
    }
}

#[must_use]
pub const fn effective_target_bits(context: TargetContext, policy: FfaTargetPolicy) -> TargetMask {
    let relation = team_relation(context);
    let mut bits = target_bits_for(context.target_kind, relation);

    if context.target_is_player && policy.allow_same_team_players && matches!(relation, TeamRelation::SameTeam) {
        bits = bits.union(TargetMask::HERO_ENEMY);
    }

    if policy.allow_same_team_objectives && matches!(relation, TeamRelation::SameTeam) {
        bits = match context.target_kind {
            UnitKind::Boss => bits.union(TargetMask::BOSS_ENEMY),
            UnitKind::Building => bits.union(TargetMask::BUILDING_ENEMY),
            _ => bits,
        };
    }

    if context.target_is_player && policy.player_boss_overlay {
        bits = match relation {
            TeamRelation::SameTeam | TeamRelation::EnemyTeam => bits.union(TargetMask::BOSS_ENEMY),
            _ => bits,
        };
    }

    bits
}

#[must_use]
pub const fn target_allowed_by_mask(
    requested_mask: TargetMask,
    context: TargetContext,
    policy: FfaTargetPolicy,
) -> bool {
    requested_mask.intersects(effective_target_bits(context, policy))
}

#[must_use]
pub const fn accepted_damage_possible(decision: LayerDecision) -> bool {
    decision.target_filter_allows && decision.damage_policy_allows
}

pub const fn validate_engine_team_plan(plan: EngineTeamPlan) -> Result<(), TeamPlanError> {
    match plan {
        EngineTeamPlan::KeepEngineTeams => Ok(()),
        EngineTeamPlan::UniqueTeamPerPlayer => {
            Err(TeamPlanError::UniqueEngineTeamsBreakServerAssumptions)
        }
    }
}

#[must_use]
const fn is_objective(kind: UnitKind) -> bool {
    matches!(kind, UnitKind::Boss | UnitKind::Building)
}

#[must_use]
const fn target_bits_for(kind: UnitKind, relation: TeamRelation) -> TargetMask {
    match kind {
        UnitKind::Hero => side_bits(relation, TargetMask::HERO_FRIENDLY, TargetMask::HERO_ENEMY),
        UnitKind::Trooper => side_bits(
            relation,
            TargetMask::TROOPER_FRIENDLY,
            TargetMask::TROOPER_ENEMY,
        ),
        UnitKind::Boss => side_bits(relation, TargetMask::BOSS_FRIENDLY, TargetMask::BOSS_ENEMY),
        UnitKind::Building => side_bits(
            relation,
            TargetMask::BUILDING_FRIENDLY,
            TargetMask::BUILDING_ENEMY,
        ),
        UnitKind::Prop => side_bits(relation, TargetMask::PROP_FRIENDLY, TargetMask::PROP_ENEMY),
        UnitKind::Minion => side_bits(
            relation,
            TargetMask::MINION_FRIENDLY,
            TargetMask::MINION_ENEMY,
        ),
        UnitKind::GoldOrb => side_bits(
            relation,
            TargetMask::GOLD_ORBS_FRIENDLY,
            TargetMask::GOLD_ORBS_ENEMY,
        ),
        UnitKind::Trophy => side_bits(
            relation,
            TargetMask::TROPHY_FRIENDLY,
            TargetMask::TROPHY_ENEMY,
        ),
        UnitKind::Neutral => TargetMask::NEUTRAL,
        UnitKind::Zipline => TargetMask::ZIPLINE,
        UnitKind::BreakableProp => TargetMask::BREAKABLE_PROP,
        UnitKind::AbilityTrigger => TargetMask::ABILITY_TRIGGER,
        UnitKind::Unknown => TargetMask::NONE,
    }
}

#[must_use]
const fn side_bits(relation: TeamRelation, friendly: TargetMask, enemy: TargetMask) -> TargetMask {
    match relation {
        TeamRelation::SameTeam => friendly,
        TeamRelation::EnemyTeam => enemy,
        TeamRelation::NeutralTarget | TeamRelation::Unknown => TargetMask::NONE,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const TEAM_AMBER: u8 = 2;
    const TEAM_SAPPHIRE: u8 = 3;

    fn same_team_player() -> TargetContext {
        TargetContext::new(Some(TEAM_AMBER), Some(TEAM_AMBER), UnitKind::Hero, true)
    }

    fn enemy_player() -> TargetContext {
        TargetContext::new(Some(TEAM_AMBER), Some(TEAM_SAPPHIRE), UnitKind::Hero, true)
    }

    #[test]
    fn target_mask_constants_match_current_re_extract() {
        assert_eq!(TargetMask::HERO_FRIENDLY.bits(), 0x1);
        assert_eq!(TargetMask::BOSS_FRIENDLY.bits(), 0x4);
        assert_eq!(TargetMask::BUILDING_FRIENDLY.bits(), 0x8);
        assert_eq!(TargetMask::HERO_ENEMY.bits(), 0x100);
        assert_eq!(TargetMask::BOSS_ENEMY.bits(), 0x400);
        assert_eq!(TargetMask::BUILDING_ENEMY.bits(), 0x800);
        assert_eq!(TargetMask::NEUTRAL.bits(), 0x10000);
        assert_eq!(TargetMask::ALL_FRIENDLY.bits(), 0x3f);
        assert_eq!(TargetMask::ALL_ENEMY.bits(), 0x13f00);
        assert_eq!(TargetMask::ALL.bits(), 0x13f3f);
    }

    #[test]
    fn raw_team_relation_is_relative_and_preserves_normal_teams() {
        assert_eq!(team_relation(same_team_player()), TeamRelation::SameTeam);
        assert_eq!(team_relation(enemy_player()), TeamRelation::EnemyTeam);
    }

    #[test]
    fn stock_enemy_mask_rejects_same_team_player() {
        assert!(!target_allowed_by_mask(
            TargetMask::HERO_ENEMY,
            same_team_player(),
            FfaTargetPolicy::disabled()
        ));
    }

    #[test]
    fn ffa_same_team_players_match_enemy_hero_mask_without_team_rewrite() {
        let context = same_team_player();
        let policy = FfaTargetPolicy::players_only();
        assert_eq!(context.source_team, Some(TEAM_AMBER));
        assert_eq!(context.target_team, Some(TEAM_AMBER));
        assert_eq!(team_relation(context), TeamRelation::SameTeam);
        assert!(target_allowed_by_mask(TargetMask::HERO_ENEMY, context, policy));
        assert_eq!(virtual_combat_relation(context, policy), CombatRelation::Enemy);
    }

    #[test]
    fn enemy_players_remain_enemies_under_normal_engine_teams() {
        assert!(target_allowed_by_mask(
            TargetMask::HERO_ENEMY,
            enemy_player(),
            FfaTargetPolicy::disabled()
        ));
        assert_eq!(
            virtual_combat_relation(enemy_player(), FfaTargetPolicy::disabled()),
            CombatRelation::Enemy
        );
    }

    #[test]
    fn same_team_objectives_are_separate_from_player_ffa() {
        let same_team_boss =
            TargetContext::new(Some(TEAM_AMBER), Some(TEAM_AMBER), UnitKind::Boss, false);
        assert!(!target_allowed_by_mask(
            TargetMask::BOSS_ENEMY,
            same_team_boss,
            FfaTargetPolicy::players_only()
        ));
        assert!(target_allowed_by_mask(
            TargetMask::BOSS_ENEMY,
            same_team_boss,
            FfaTargetPolicy::players_and_objectives()
        ));
    }

    #[test]
    fn objective_policy_covers_bosses_and_buildings_but_not_all_friendly_entities() {
        let policy = FfaTargetPolicy::players_and_objectives();
        let same_team_building = TargetContext::new(
            Some(TEAM_AMBER),
            Some(TEAM_AMBER),
            UnitKind::Building,
            false,
        );
        let same_team_trooper = TargetContext::new(
            Some(TEAM_AMBER),
            Some(TEAM_AMBER),
            UnitKind::Trooper,
            false,
        );
        assert!(target_allowed_by_mask(
            TargetMask::BUILDING_ENEMY,
            same_team_building,
            policy
        ));
        assert!(!target_allowed_by_mask(
            TargetMask::TROOPER_ENEMY,
            same_team_trooper,
            policy
        ));
    }

    #[test]
    fn neutral_targets_are_category_based_not_team_two_or_three() {
        let neutral = TargetContext::new(Some(TEAM_AMBER), Some(TEAM_AMBER), UnitKind::Neutral, false);
        assert_eq!(team_relation(neutral), TeamRelation::NeutralTarget);
        assert!(!target_allowed_by_mask(
            TargetMask::HERO_ENEMY,
            neutral,
            FfaTargetPolicy::players_and_objectives()
        ));
        assert!(target_allowed_by_mask(
            TargetMask::NEUTRAL,
            neutral,
            FfaTargetPolicy::players_and_objectives()
        ));
    }

    #[test]
    fn unknown_teams_fail_closed_for_hero_masks() {
        let unknown = TargetContext::new(None, Some(TEAM_AMBER), UnitKind::Hero, true);
        assert_eq!(team_relation(unknown), TeamRelation::Unknown);
        assert!(!target_allowed_by_mask(
            TargetMask::HERO_ENEMY,
            unknown,
            FfaTargetPolicy::players_only()
        ));
    }

    #[test]
    fn midboss_alike_players_are_virtual_overlay_not_class_mutation() {
        let context = same_team_player();
        let policy = FfaTargetPolicy::players_only().with_player_boss_overlay();
        assert_eq!(context.target_kind, UnitKind::Hero);
        assert!(context.target_is_player);
        assert!(effective_target_bits(context, policy).contains(TargetMask::BOSS_ENEMY));
        assert!(target_allowed_by_mask(TargetMask::BOSS_ENEMY, context, policy));
    }

    #[test]
    fn midboss_overlay_does_not_make_players_neutral() {
        let policy = FfaTargetPolicy::players_only().with_player_boss_overlay();
        assert!(!effective_target_bits(same_team_player(), policy).contains(TargetMask::NEUTRAL));
        assert!(!target_allowed_by_mask(
            TargetMask::NEUTRAL,
            same_team_player(),
            policy
        ));
    }

    #[test]
    fn ffa_does_not_use_unique_engine_teams() {
        assert_eq!(validate_engine_team_plan(EngineTeamPlan::KeepEngineTeams), Ok(()));
        assert_eq!(
            validate_engine_team_plan(EngineTeamPlan::UniqueTeamPerPlayer),
            Err(TeamPlanError::UniqueEngineTeamsBreakServerAssumptions)
        );
    }

    #[test]
    fn damage_policy_cannot_create_a_hit_rejected_by_target_filter() {
        assert!(!accepted_damage_possible(LayerDecision {
            target_filter_allows: false,
            damage_policy_allows: true,
        }));
        assert!(accepted_damage_possible(LayerDecision {
            target_filter_allows: true,
            damage_policy_allows: true,
        }));
    }

    #[test]
    fn every_current_side_unit_kind_has_friendly_and_enemy_bits() {
        for kind in [
            UnitKind::Hero,
            UnitKind::Trooper,
            UnitKind::Boss,
            UnitKind::Building,
            UnitKind::Prop,
            UnitKind::Minion,
            UnitKind::GoldOrb,
            UnitKind::Trophy,
        ] {
            let friendly = TargetContext::new(Some(TEAM_AMBER), Some(TEAM_AMBER), kind, false);
            let enemy = TargetContext::new(Some(TEAM_AMBER), Some(TEAM_SAPPHIRE), kind, false);
            assert_ne!(effective_target_bits(friendly, FfaTargetPolicy::disabled()), TargetMask::NONE);
            assert_ne!(effective_target_bits(enemy, FfaTargetPolicy::disabled()), TargetMask::NONE);
            assert!(!effective_target_bits(friendly, FfaTargetPolicy::disabled())
                .intersects(effective_target_bits(enemy, FfaTargetPolicy::disabled())));
        }
    }
}
