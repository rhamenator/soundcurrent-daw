# ADR093: frozen checked copy requests and explicit uncertain outcomes

Status: selected; Linux/native Windows qualification at PR71 exact head c1a3717.
Date: 2026-10-09.

| Alternative | Functionality / licensing / maintenance / integration cost | Decision |
|---|---|---|
| Invoke the fresh-check developer CLI after a GUI check | Could silently approve changed audio/inspection | Refused |
| Treat canceled or nonzero child exit as no copy | Races exclusive receipt publication and lost output | Refused |
| Parse/validate/copy/recover on the GUI or RT callback | Blocks interaction/audio and mixes ownership | Refused |
| Introduce another framework or transaction library | New license/source-delivery and platform costs without demonstrated need | No new dependency |
| Original GPL bounded frozen request/reply codec + existing controller/native transaction infrastructure | Exact occurrence/hash checks before mutation, independent child ownership and explicit checked recovery; UI/storage/native qualification required | Selected |

Keep GUI/control, child, source paths, owned receipt and future Session adoption
as distinct authorities. A reply checksum/PID is not OS sandboxing or a signature.
A failed child with destination access requires independent recovery. A newly
checked row cannot inherit a previously copied selection's state. Clear/close
releases local choices without deleting external operation folders.

Existing Qt, JSON, crypto, libsndfile and GPL modules retain their pins/notices.
The test-only parked/flood/lost-report child is built separately from the installed
worker and is not part of installation. No equalizer code or external asset is
newly copied. Source delivery and native/installed qualification remain required;
see checkpoint 127 for costs, measurable acceptance and next semantic conversion.
