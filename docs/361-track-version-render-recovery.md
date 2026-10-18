# Inactive track-version render recovery

The retained stretch-render inventory now checks inactive audio arrangements when
classifying a verified operation as already attached. Switching to an empty
version must not mislabel a retained render as stale. Classification still occurs
after manifest, source binding, intent, and rendered-media verification; an
inactive reference cannot bypass those checks. Already attached operations do not
produce a new adoptable result.

The focused controller regression creates a rendered arrangement, switches to an
empty Unicode-named version, saves and reopens, checks attachment classification,
checks missing-media refusal and byte-preserving restoration, removes the inactive
version, and restores it through Undo. Removal retains the media on disk but no
longer counts the unreferenced render as attached. Original manifest bytes remain
unchanged. This does not implement automatic relinking or garbage collection.

Validation status: source/diff checks complete; focused controller execution and
hosted Linux/Windows CI pending. No installed preview has changed. Windows preview
qualification remains pinned to its existing producer revision and can proceed
independently. This increment does not establish full recovery or track-version
parity.
