# Translation review — 2026-10-07

## Current catalog status — 2026-10-08

All 524 currently extracted source messages have populated translations in all 33 non-English catalogs, including Nynorsk. There are 34 catalogs including English. This count describes catalog coverage; it does not establish that every user-facing string has been extracted. Missing interface strings remain a separate second pass, as requested.

The dated checkpoints below document earlier states and contextual AI review. Their incomplete-language counts are historical, not current status. Automated structural and compiled-catalog checks do not prove linguistic accuracy. Native-speaker verification remains unverified for every non-English locale. Runtime qualification is tracked separately in tests/results/localization. Current Linux lifecycle checks pass on Ubuntu, Fedora and AlmaLinux fixtures. Both current application builds passed Windows installed-package fixtures. An older EQ binary triggered a Defender detection whose false-positive status remains unresolved. Visual review is partial and interactive installer/startup qualification remains pending. No release is authorized by this checkpoint.

## Scope and evidence

A contextual AI review examined the 30 starter strings in each of 32 unverified languages (960 translated entries). This is an internal review, not an independent review or native-speaker certification. No external translator service was used. Remaining untranslated strings were not certified by this review.

Corrections address clear semantic risks and ambiguous audio terminology: Swahili gain previously used a word for profit and bands a word for belts; frequency-band labels were made explicit in several languages; flat-response labels/reset actions were clarified; Hungarian regional settings and Vietnamese audio/preset labels were improved. These replacement translations remain unverified themselves.

`data/localization/translation-context.json` supplies technical definitions in generated Qt Linguist translator notes. Gain means a signed signal-level adjustment in dB; flat means zero EQ gain, not mute; balance means left/right channel level; Quit exits the process while closing the window leaves it running. Notes are shared between the applications and can be reused by the DAW.

## Remaining uncertainty

French, German, Spanish, Italian and both Portuguese variants, Dutch and Polish are populated; the other 24 language packs remain incomplete. All 32 lack native review. Particular review priorities are Swahili audio vocabulary, Thai preset wording, natural microphone EQ terminology in every language, and regional Portuguese and Chinese usage. An AI reread or back-translation by the same model is not independent evidence and must not be labeled native review. Do not promote catalog review status based on this pass.

## Required review workflow

1. Review the English source and actual control behavior with the translator notes. Resolve ambiguous source wording before translating it.
2. A fluent reviewer should compare each target string directly with that intent and flag wrong meaning, unintended connotations, register, regional usage, spelling and unnatural phrasing. Back-translation can help identify questions, but does not replace this comparison.
3. Check strings in the running interface, including RTL, truncation, mnemonics, decimal entry, and complete error messages. Verify Quit, reset and lock semantics explicitly.
4. Record reviewer identity/locale, date, catalog commit, corrections and unresolved issues before setting nativeReviewed. Any later edited strings require renewed review.
5. Keep uncertain entries unfinished so they fall back to English. Keep partial-language coverage visible. Full language qualification also requires the release gates in localization.md.

## Automated checks

Catalog audit checks source inventory, duplicate keys, nonempty finished entries, exact placeholders, literal `&&` preservation, absence of hidden bidi override/isolate controls, coverage and compiled-catalog hashes. The interface supplies RTL direction explicitly. These checks verify structural integrity, not translation accuracy or cultural appropriateness.

## Localization completion checkpoint

The French catalog is now populated for all currently extracted app messages (437 EQ / 523 Studio). Translation and contextual review were performed by this coding agent; no external provider or fluent-speaker certification was used. Coverage includes help, equipment validation, calibration preview, standard Qt actions and Studio render/status text. Every French entry remains unverified. German, Spanish, Italian and both Portuguese variants are also populated; the other 26 languages retain their starter coverage. The independent localization-guard audit agrees with populated coverage; its linguistic/runtime qualification fields remain unknown because that tool cannot assess them. Compiled/runtime evidence is recorded separately in tests/results/localization/checkpoint.json.

## German completion checkpoint

All current EQ/Studio catalog entries are populated in German (437 / 523). This coding agent reviewed the supplied text against actual control meaning: Verstärkung is audio gain; Pegelreserve is headroom; Ohne Effekt is dry signal; Pegelkorrektur is channel trim, not file cutting; Loudness-Korrektur is the listening preset, not a loudness measurement; Rückkopplung is delay feedback. Profile type IDs, user/vendor names, rich-text links, extensions, placeholders and saved processing values remain invariant. Filter Q replaces the imprecise Width (Q) source caption. This is contextual AI review and remains unverified, not native-speaker certification.

## Spanish completion checkpoint

All current catalog entries are populated in Spanish (437 EQ / 523 Studio). Contextual AI review distinguishes Ganancia (audio gain), Factor Q del filtro (dimensionless filter quality), Plano (zero EQ gain), Compensación de sonoridad (low-volume listening correction), Sin efecto (dry signal), and Tiempo de relajación del compresor (compressor release). Manufacturer names, profiles, placeholders, URLs and file filters remain invariant. These translations remain unverified; this does not constitute native-speaker review.

## Italian completion checkpoint

All current extracted entries are populated (437 EQ / 523 Studio). Contextual AI review distinguishes Guadagno (signed audio gain), Fattore Q del filtro, Piatto (zero EQ gain), Compensazione loudness (low-volume listening correction), Senza effetto (dry signal), Rilascio del compressore and Annulla modifica (undo, distinct from Annulla for cancellation). Stable IDs, vendor/user metadata, links, file filters and placeholders remain unchanged. No native-speaker certification was obtained; all entries remain unverified.

## Portuguese regional completion checkpoint

Both pt-PT and pt-BR catalogs populate all currently extracted messages (437 EQ / 523 Studio each). Regional contextual review uses coluna/alto-falante, auscultadores/fones de ouvido, ficheiro/arquivo, guardar/salvar, controlos/controles, recetor/receptor, varrimento/varredura and percentagem/porcentagem. Technical intent preserves zero-gain Plano, low-volume Compensação loudness, Sem efeito for dry signal and signed gain adjustments. Compressor release is Tempo de recuperação do compressor in pt-PT and Tempo de liberação do compressor in pt-BR. This is contextual AI review, unverified; no native-speaker certification is claimed.

## Dutch completion checkpoint

All current extracted entries are populated (437 EQ / 523 Studio). Contextual AI review uses Versterking for signed audio gain, Filter-Q for quality factor, Vlak for zero EQ gain, Loudnesscompensatie for low-volume listening compensation, Zonder effect for dry signal, Hersteltijd van compressor for release and Ongedaan maken for undo. Vendor metadata, machine IDs, file filters, placeholders and source links remain invariant. All entries remain unverified; no native-speaker review is claimed.

## Polish completion checkpoint

All current extracted entries are populated (437 EQ / 523 Studio). Contextual AI review uses Wzmocnienie for signed audio gain, Dobroć filtra Q for dimensionless quality factor, Płaska charakterystyka for zero EQ gain, Kompensacja loudness for low-volume listening compensation, Bez efektu for dry signal, and Czas powrotu kompresora for release. Render counts use Wyrenderowane kanały: %1 to avoid an incorrect fixed grammatical ending. Placeholder-containing route and microphone captions avoid requiring declension of inserted names. Machine IDs, file filters, links and saved parameters remain invariant. All entries remain unverified; no native-speaker review is claimed.

## Czech and Slovak initial batch

Reviewed prompts against actual import, correction-preview, calibration and audio setup behavior. Signed gain uses zesílení/zosilnenie; headroom uses rezerva; meter text retains the estimated-level qualifier. Dynamic route types use a neutral label construction to avoid inflecting inserted names. Device identities, placeholders and numerical settings remain unchanged. Both catalogs remain incomplete and unverified; no native-speaker review is claimed.

Czech/Slovak second batch: wet mix is the effect contribution, not total output volume; delay time is distinct from decay time. Sweep error text explicitly describes a continuously changing test frequency. Failure prompts preserve the conditions under which processing is refused and the app stays open. This is contextual AI review, not native-speaker verification.

Czech/Slovak filter and measurement review: Q is a dimensionless quality factor, shelving differs from pass filtering, compressor attack/release differs from effect decay, and makeup is gain compensation. Loudness describes low-volume tonal compensation. Estimated level labels retain that qualifier. Published model correction remains additive to listening EQ and does not claim room/amplifier calibration. Native-speaker review remains unverified.

## Czech and Slovak completed extracted catalogs

All current extracted entries populated (441 EQ / 527 Studio). The review distinguishes compressor makeup from post gain, pass filters from shelving, measured response from inverted EQ correction and effects from microphone correction. Render summary uses a label form for arbitrary counts. Calibration warnings retain the microphone contribution and bounded suggested gain. File filters, markup URLs, placeholders, IDs and saved numeric parameters are unchanged. Catalogs remain unverified; no native-speaker review is claimed.

## Ukrainian and Russian initial batches

Translated independently with Ukrainian підсилення/чіткість and Russian усиление/чёткость. Dynamic equipment names use neutral constructions to avoid forcing noun declension. Inspection of enhancement.cpp confirms ambience damping reduces high-frequency content in the feedback path; decay describes duration. Estimated-meter qualifiers, clipping warnings and safe failure/paused-state messages remain explicit. This is contextual AI review; both catalogs are incomplete and native-speaker verification remains unverified.

Ukrainian/Russian filter and measurement review: добротність/добротность identifies dimensionless filter Q; компенсаційне/компенсационное підсилення/усиление distinguishes compressor makeup from post gain. Тонкомпенсація/тонкомпенсация means low-volume tone compensation. Negative route gain explicitly reverses polarity. Calibration continues to require a system measurement for room/amplifier effects, and labels distinguish estimated output from measured microphone response. Catalogs remain unverified.

Ukrainian/Russian startup and render review: render-channel and clipped-sample counts use labels to avoid a fixed plural suffix. Profile editing preserves the measured reference and saves a custom copy. Quit/reopen remains distinct from closing the window. Device-setup changes retain the restart warning and possible playback interruption. Structural checks preserve source hyperlinks and placeholders; native-speaker verification remains unverified.

## Ukrainian and Russian completed extracted catalogs

All currently extracted messages populated (441 EQ / 527 Studio). Q uses добротність/добротность; signed gain uses підсилення/усиление; tone compensation uses тонкомпенсація/тонкомпенсация. Wet mix remains effect contribution, compressor makeup differs from post gain, and measurement results retain microphone influence. Rendering counts use neutral labels. Warnings preserve restart, quiet-start and clipping behavior. Catalogs remain unverified; native-speaker review is not claimed.

## Greek and Turkish initial batch

Greek ενίσχυση and Turkish kazanç describe audio gain; ψαλιδισμός/kırpılma describes clipping. Headroom is περιθώριο στάθμης/seviye payı, distinct from user balance and post gain. Ambience damping refers to high-frequency absorption in feedback and decay to duration. Dynamic model names use neutral route-type constructions. Error messages retain paused processing, kept settings and restart guidance. Contextual AI review remains unverified, with no native-speaker claim.

Greek/Turkish recovery review: processing refusal stays explicit when equalizer inspection fails. File limits and protected settings remain unchanged. Turkish cancel-render text refers to creating an audio file, distinct from live processing. Decay describes effect duration, while wet mix describes contribution. Catalogs remain incomplete and native review unverified.

Greek/Turkish filter/editor review: συντελεστής ποιότητας φίλτρου Q/filtre kalite faktörü Q describes quality, not bandwidth. Υψιπερατό/yüksek geçiren differs from shelving. Compressor compensation and release are distinguished from post gain and effect decay. Estimated-meter labels retain uncertainty, and quitting explicitly restores normal audio. Native review remains unverified.

Greek/Turkish measurement review: loudness is low-volume tonal compensation rather than a global volume increase. Greek offline rendering describes non-real-time processing to avoid implying an internet requirement. Negative route gain means polarity inversion. Correction remains additive and microphone clipping requires reducing input gain/boost. Preset and profile limits preserve numerical meaning; native review remains unverified.

Greek/Turkish startup/render review: sign-in differs from a boot-time service; enabling one app replaces the shared registration. Quitting differs from closing the window. Profile-save instructions preserve the reference copy. Rendering summaries use labels for arbitrary channel and clipped-sample counts. Link text is localized while href attributes remain unchanged. Native review remains unverified.

## Greek and Turkish completed extracted catalogs

All currently extracted messages populated (441 EQ / 527 Studio). Q remains dimensionless quality, headroom is level margin, compressor makeup differs from post gain, and wet mix is effect contribution. Low-volume loudness compensation differs from output volume. Relative measurements include the microphone; measured response differs from inverted correction. Rendering summaries use labels for arbitrary counts. URLs, file filters, placeholders, machine IDs and numerical settings are preserved. Catalogs remain unverified; native-speaker review is not claimed.

## Swedish and Danish initial batch

Regional terms reviewed separately: Swedish förstärkning/klippning and Danish forstærkning/klipning. Headroom is nivåmarginal/niveaumargin, not balance or overall volume. Meter text retains estimated levels and peak warnings. Damping refers to high-frequency absorption while decay refers to duration. Amplifier profiles retain the requirement for actual electrical measurements. Native-speaker review remains unverified; both catalogs are incomplete.

Swedish/Danish recovery review: refusing processing when active-equalizer inspection fails remains explicit. Preset names are distinguished from built-in identifiers, file-size limits remain unchanged, and app-open recovery behavior is retained. Avklingningstid/henfaldstid describes effect decay; effektandel describes processed effect contribution. Native review remains unverified.

Swedish/Danish filter review: kvalitetsfaktor identifies dimensionless Q rather than bandwidth. Hyllfilter/shelving-filter differs from högpassfilter/højpasfilter. Kompensationsförstärkning/kompensationsforstærkning is compressor makeup; decay time is not compressor release. Estimated-meter labels retain uncertainty. Exit actions retain normal-audio restoration. Native review remains unverified.

Swedish/Danish measurement review: loudnesskompensation describes low-volume tonal compensation. Negative linear route gain is explicit polarity inversion. Balance attenuates the opposite channel rather than boosting the selected one. Measurement conditions, conservative model corrections and microphone clipping warnings preserve their original limits. Native review remains unverified.

Swedish/Danish startup/render review: sign-in remains a per-user registration; selecting one SoundCurrent app replaces the other registration. Saving an edited equipment response creates a custom copy and preserves the reference. Offline rendering remains distinct from live processing. Render summaries use neutral count labels. Calibration links retain their exact destinations. Native review remains unverified.

Swedish/Danish import and speaker-profile review: frekvensgång/frekvensgang describes frequency response. Imported points require increasing frequencies and finite bounded values; numeric limits and file globs are unchanged. Efterklang/rumklang describes reverb, with effect contribution distinct from output gain. Restart guidance and automatic-output fallback remain explicit. Native review remains unverified.

## Swedish and Danish completed extracted catalogs

All currently extracted messages populated (441 EQ / 527 Studio). Q remains dimensionless quality, headroom is level margin, compressor makeup differs from post gain, and wet mix is effect contribution. Low-volume loudness compensation differs from output volume. Relative measurements include the microphone; measured response differs from inverted correction. Rendering summaries use labels for arbitrary counts. URLs, file filters, placeholders, machine IDs and numerical settings are preserved. Catalogs remain unverified; native-speaker review is not claimed.

## Norwegian Bokmål and Finnish initial batch

Forsterkning/vahvistus describes gain; nivåmargin/tasovara describes headroom, distinct from balance. Klipping/leikkautuminen describes clipping. Room-effect damping and decay retain separate controls, and dynamic boost retains its peak ceiling. Amplifier profiles still require electrical measurements with a known load; marketing specifications are insufficient. Setup recovery retains paused processing and the open app. Placeholders, units and character limits are preserved. Native-speaker review remains unverified.

Norwegian Bokmål/Finnish recovery review: failures to inspect active equalizers explicitly prevent processing. Setup errors preserve an open app and Windows restart guidance. File-size limits and half-dB steps retain their original values. Avklingningstid/vaimenemisaika describes decay duration; effektandel/efektin osuus describes processed effect contribution. Render cancellation remains distinct from disabling live processing. Native review remains unverified.

Norwegian Bokmål/Finnish filter review: kvalitetsfaktor/laatutekijä describes dimensionless Q rather than bandwidth. Høypassfilter/ylipäästösuodatin differs from shelving. Kompensasjonsforsterkning/kompensointivahvistus describes compressor makeup, separate from post gain. Meter captions retain estimated levels and potential clipping; exit actions restore ordinary audio. Native review remains unverified.

Norwegian Bokmål/Finnish measurement review: loudness compensation remains tonal compensation rather than overall volume. Negative route gain explicitly inverts polarity. Balance attenuates the opposite channel without boosting the selected one; captions use V/H in Norwegian and V/O in Finnish. Model correction remains additive, system effects need measurement, and microphone clipping requires reduced input gain or boost. Limits and schema tokens are preserved. Native review remains unverified.

Norwegian Bokmål/Finnish startup and rendering review: sign-in remains per-user, and selecting one app replaces the other shared registration. Closing the window differs from quitting. Profile edits save a custom copy while preserving the reference. Measured response differs from correction at 48 kHz. Render summaries use count labels without fixed plural endings. Calibration link destinations remain unchanged. Native review remains unverified.

Norwegian Bokmål/Finnish import and speaker-profile review: frekvensrespons/taajuusvaste describes measured frequency response. Imported points require increasing frequencies and finite bounded values; numerical limits and file globs remain unchanged. Romklang/kaikunta describes reverb, distinct from delayed echo and output gain. Device disconnection explicitly switches to automatic output; Windows restart guidance remains intact. Native review remains unverified.

## Norwegian Bokmål and Finnish completed extracted catalogs

All currently extracted messages populated (441 EQ / 527 Studio). Q remains dimensionless quality, headroom is level margin, compressor makeup differs from post gain, and wet mix is effect contribution. Low-volume loudness compensation differs from output volume. Relative measurements include the microphone; measured response differs from inverted correction. Rendering summaries use labels for arbitrary counts. URLs, file filters, placeholders, machine IDs and numerical settings are preserved. Catalogs remain unverified; native-speaker review is not claimed.

## Romanian and Hungarian initial batch

Câștig/erősítés describes gain; rezervă de nivel/szinttartalék describes headroom, distinct from balance. Clipping warnings retain potential distortion, while dynamic boost retains its peak ceiling. Ambience damping and decay remain separate controls. Amplifier profiles require electrical measurements with known load and settings; marketing specifications are insufficient. Setup recovery keeps processing paused and the app open. Placeholders, units and character limits remain unchanged. Native review remains unverified.

Romanian/Hungarian recovery review: failures to inspect active equalizers explicitly prevent processing. Setup recovery preserves an open app. File-size limits and half-dB steps retain their values. Timp de stingere/lecsengési idő describes duration; proporția efectului/effekt aránya describes processed contribution. Romanian clipping warnings describe cut-off peaks, distinct from an intentional limiter. Native review remains unverified.

Romanian/Hungarian filter review: factor de calitate/jósági tényező identifies dimensionless Q rather than bandwidth. Pass filters differ from shelving. Câștig de compensare/kompenzációs erősítés describes compressor makeup, distinct from post gain. Attack and release describe compressor timing, separate from effect decay. Meter labels retain estimated levels and potential clipping; exit actions restore normal audio. Native review remains unverified.

Romanian/Hungarian measurement review: loudness compensation remains tonal compensation rather than overall volume. Negative route gain explicitly inverts polarity. Balance attenuates the opposite channel without boosting the selected one; captions use S/D in Romanian and B/J in Hungarian. Model correction remains additive, system effects need measurement, and microphone clipping requires reduced input gain or boost. Limits and schema tokens are preserved. Native review remains unverified.

Romanian/Hungarian startup and rendering review: sign-in remains per-user, and selecting one app replaces the shared registration. Closing the window differs from quitting. Profile edits save a custom copy while preserving the reference. Measured response differs from correction at 48 kHz. Render summaries use count labels without fixed plural endings. Calibration link destinations remain unchanged. Native review remains unverified.

Romanian/Hungarian import and speaker-profile review: răspuns/frekvenciamenet describes measured frequency response. Imported points require increasing frequencies and finite bounded values; numerical limits and file globs remain unchanged. Reverberație/zengetés describes reverb, distinct from delayed echo and output gain. Device disconnection explicitly switches to automatic output; Windows restart guidance remains intact. Native review remains unverified.

## Romanian and Hungarian completed extracted catalogs

All currently extracted messages populated (441 EQ / 527 Studio). Q remains dimensionless quality, headroom is level margin, compressor makeup differs from post gain, and wet mix is effect contribution. Low-volume loudness compensation differs from output volume. Relative measurements include the microphone; measured response differs from inverted correction. Rendering summaries use labels for arbitrary counts. URLs, file filters, placeholders, machine IDs and numerical settings are preserved. Catalogs remain unverified; native-speaker review is not claimed.

## Nynorsk initial batch

Nynorsk is its own written standard and catalog, separate from Bokmål. Wording uses innstillingar, einingar, forsterking and førehandsinnstilling consistently. Nivåmargin remains distinct from balance, and clipping warnings retain estimated peak risk. Amplifier profiles require electrical measurement, not marketing specifications. Setup recovery preserves an open app with paused processing. Regional language selection retains nn and nb separately. Native review remains unverified.

Nynorsk recovery review: inability to inspect active equalizers explicitly prevents handsaming. Setup errors keep the app open. Limits and half-dB steps retain their values. Avklingingstid describes duration, while effektdel describes processed contribution. Render cancellation remains distinct from disabling live playback processing. Native review remains unverified.

Nynorsk filter review: kvalitetsfaktor identifies dimensionless Q, distinct from bandwidth. Høgpassfilter differs from hyllefilter. Kompensasjonsforsterking identifies compressor makeup, separate from post gain. Attack/release timing remains separate from effect decay. Meter captions retain estimated levels and possible clipping; exit restores normal audio. Native review remains unverified.

Nynorsk measurement review: loudness compensation remains low-volume tonal compensation rather than overall volume. Negative route gain explicitly reverses polarity. Balance attenuates the opposite channel without boosting the selected one; endpoint captions use V/H. Model correction remains additive, system effects require measurement, and microphone clipping requires lower input gain or boost. Numerical limits and schema tokens are preserved. Native review remains unverified.

Nynorsk startup/render review: innlogging describes user sign-in rather than system boot. Enabling one app replaces the shared registration. Profile saving preserves the reference and creates an eigendefinert kopi. Render summaries use neutral labels for arbitrary counts. Measured response differs from correction at 48 kHz; measurement suggestions retain the 3 dB limit. Link destinations and placeholders are unchanged. Native review remains unverified.

Nynorsk import and speaker-profile review: frekvensrespons describes measured response. Imported points require increasing frequencies and finite bounded values; numerical limits and file globs remain unchanged. Romklang describes reverb, distinct from delayed echo and output gain. Device disconnection explicitly switches to automatic output; Windows restart guidance remains intact. Native review remains unverified.

## Nynorsk completed extracted catalogs

All currently extracted messages populated (441 EQ / 527 Studio). Q remains dimensionless quality, headroom is level margin, compressor makeup differs from post gain, and wet mix is effect contribution. Low-volume loudness compensation differs from output volume. Relative measurements include the microphone; measured response differs from inverted correction. Rendering summaries use labels for arbitrary counts. URLs, file filters, placeholders, machine IDs and numerical settings are preserved. Catalogs remain unverified; native-speaker review is not claimed.

## Arabic initial batch

الكسب describes gain; هامش المستوى describes headroom, distinct from channel balance. قص قمم الإشارة describes clipped signal peaks. Room-effect damping differs from decay duration. Amplifier curves require electrical measurements with known load and settings; marketing specifications are insufficient. Processing pauses during setup and recovery keeps the app open. Placeholders, units and file/character limits remain unchanged. Arabic EQ was sampled at 1280×720; full bidi and native-speaker review remain unverified.
