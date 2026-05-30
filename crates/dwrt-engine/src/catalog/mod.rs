mod defaults;

use crate::{Authority, HotPathPolicy, ServerSurface, SurfaceDescriptor};

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SurfaceCatalog {
    surfaces: Vec<SurfaceDescriptor>,
}

impl SurfaceCatalog {
    #[must_use]
    pub fn deadlock_defaults() -> Self {
        Self {
            surfaces: default_surfaces().to_vec(),
        }
    }

    pub fn iter(&self) -> impl Iterator<Item = &SurfaceDescriptor> {
        self.surfaces.iter()
    }

    #[must_use]
    pub fn get(&self, surface: ServerSurface) -> Option<&SurfaceDescriptor> {
        self.surfaces
            .iter()
            .find(|descriptor| descriptor.surface == surface)
    }

    pub fn engine_authoritative(&self) -> impl Iterator<Item = &SurfaceDescriptor> {
        self.surfaces
            .iter()
            .filter(|descriptor| descriptor.authority == Authority::Engine)
    }

    pub fn hot_surfaces(&self) -> impl Iterator<Item = &SurfaceDescriptor> {
        self.surfaces
            .iter()
            .filter(|descriptor| descriptor.hot_path != HotPathPolicy::NotHot)
    }

    pub fn mutatable_by_default(&self) -> impl Iterator<Item = &SurfaceDescriptor> {
        self.surfaces
            .iter()
            .filter(|descriptor| descriptor.can_mutate())
    }
}

#[must_use]
pub fn default_surfaces() -> &'static [SurfaceDescriptor] {
    defaults::DEFAULT_SURFACES
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::DiscoveryKind;

    #[test]
    fn catalog_contains_core_server_surfaces() {
        let catalog = SurfaceCatalog::deadlock_defaults();
        assert!(catalog.get(ServerSurface::Source2Networking).is_some());
        assert!(catalog.get(ServerSurface::EntitySimulation).is_some());
        assert!(catalog.get(ServerSurface::ControllerPawn).is_some());
        assert!(catalog.get(ServerSurface::UsercmdPipeline).is_some());
        assert!(catalog.get(ServerSurface::NetMessagePipeline).is_some());
    }

    #[test]
    fn engine_authoritative_surfaces_are_not_replaced_by_default() {
        let catalog = SurfaceCatalog::deadlock_defaults();
        for descriptor in catalog.engine_authoritative() {
            assert!(
                !descriptor.can_replace(),
                "{:?} must not be replaceable by default",
                descriptor.surface
            );
        }
    }

    #[test]
    fn hot_surfaces_are_mounted_or_interest_gated() {
        let catalog = SurfaceCatalog::deadlock_defaults();
        for descriptor in catalog.hot_surfaces() {
            assert!(
                descriptor.requires_interest_gate()
                    || descriptor.hot_path == HotPathPolicy::RuntimeInternalOnly,
                "{:?} has no hot-path policy",
                descriptor.surface
            );
        }
    }

    #[test]
    fn dangerous_core_systems_are_observe_only_by_default() {
        let catalog = SurfaceCatalog::deadlock_defaults();
        for surface in [
            ServerSurface::Source2Networking,
            ServerSurface::SteamAuth,
            ServerSurface::SnapshotGeneration,
            ServerSurface::Prediction,
        ] {
            let descriptor = catalog.get(surface).unwrap();
            assert!(descriptor.can_observe());
            assert!(
                !descriptor.can_mutate(),
                "{surface:?} unexpectedly mutatable"
            );
            assert!(
                !descriptor.can_replace(),
                "{surface:?} unexpectedly replaceable"
            );
        }
    }

    #[test]
    fn player_and_rules_facades_are_curated_mutation_surfaces() {
        let catalog = SurfaceCatalog::deadlock_defaults();
        assert!(
            catalog
                .get(ServerSurface::ControllerPawn)
                .unwrap()
                .can_mutate()
        );
        assert!(catalog.get(ServerSurface::GameRules).unwrap().can_mutate());
        assert!(
            catalog
                .get(ServerSurface::ControllerPawn)
                .unwrap()
                .discovery
                .contains(&DiscoveryKind::Schema)
        );
    }
}
