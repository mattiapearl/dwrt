use std::fmt;
use std::marker::PhantomData;
use std::num::NonZeroUsize;
use std::rc::Rc;

/// Non-owning engine pointer tagged with an entity type.
///
/// `EngineRef` is intentionally `!Send`/`!Sync` via `Rc` marker because engine
/// object access is assumed server-thread-only until a specific engine function
/// is proven thread-safe.
#[derive(Eq, PartialEq, Ord, PartialOrd, Hash)]
pub struct EngineRef<T> {
    address: NonZeroUsize,
    _entity: PhantomData<fn() -> T>,
    _server_thread_only: PhantomData<Rc<()>>,
}

impl<T> EngineRef<T> {
    /// Creates a typed engine reference from a raw address.
    ///
    /// # Safety
    ///
    /// The caller must ensure the address is a valid live engine object of type
    /// `T` for the duration of the scoped wrapper using it. This function does
    /// not dereference the address.
    pub unsafe fn from_addr(address: usize) -> Option<Self> {
        NonZeroUsize::new(address).map(|address| Self {
            address,
            _entity: PhantomData,
            _server_thread_only: PhantomData,
        })
    }

    #[must_use]
    pub const fn addr(self) -> usize {
        self.address.get()
    }
}

impl<T> Clone for EngineRef<T> {
    fn clone(&self) -> Self {
        *self
    }
}

impl<T> Copy for EngineRef<T> {}

impl<T> fmt::Debug for EngineRef<T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_tuple("EngineRef")
            .field(&format_args!("0x{:X}", self.address))
            .finish()
    }
}
