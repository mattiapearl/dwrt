#[derive(Clone, Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub struct BuildId {
    pub app: String,
    pub executable_sha256: Option<String>,
    pub product_version: Option<String>,
    pub build_number: Option<String>,
}

impl BuildId {
    #[must_use]
    pub fn new(app: impl Into<String>) -> Self {
        Self {
            app: app.into(),
            executable_sha256: None,
            product_version: None,
            build_number: None,
        }
    }

    #[must_use]
    pub fn with_executable_sha256(mut self, sha256: impl Into<String>) -> Self {
        self.executable_sha256 = Some(sha256.into());
        self
    }

    #[must_use]
    pub fn with_product_version(mut self, version: impl Into<String>) -> Self {
        self.product_version = Some(version.into());
        self
    }

    #[must_use]
    pub fn with_build_number(mut self, build_number: impl Into<String>) -> Self {
        self.build_number = Some(build_number.into());
        self
    }
}

#[derive(Clone, Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub struct ModuleId {
    pub name: String,
    pub image_size: Option<u64>,
    pub timestamp: Option<u32>,
    pub sha256: Option<String>,
}

impl ModuleId {
    #[must_use]
    pub fn new(name: impl Into<String>) -> Self {
        Self {
            name: name.into(),
            image_size: None,
            timestamp: None,
            sha256: None,
        }
    }
}
