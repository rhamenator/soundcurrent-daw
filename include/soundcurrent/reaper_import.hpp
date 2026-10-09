// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "reaper_structure.hpp"

namespace soundcurrent::daw {
// Stable, language-independent schema IDs. Values are in the ORIGINAL suite's
// units. Preserved means retained/decoded, never equivalent destination DSP.
enum class ImportObjectKind { Project, Track, Item, Source };
enum class ImportPropertyId : unsigned {
    ProjectSampleRate = 1, ProjectSampleRateEnabled = 2,
    TrackIdentity = 3, TrackName = 4, TrackGain = 5, TrackPan = 6, TrackChannels = 7,
    ItemIdentity = 8, ItemPosition = 9, ItemLength = 10, ItemFadeIn = 11, ItemFadeOut = 12,
    TakeName = 13, ItemGain = 14, TakeGain = 15, TakePan = 16, TakeSourceOffset = 17,
    TakeRate = 18, TakePitch = 19, SourceFile = 20
};
enum class ImportValueKind { None, Number, Bytes };
enum class ImportEvidenceStatus { Preserved, Converted, Unsupported, Missing, Unverified };
enum class ImportEvidenceReason {
    OriginalValue, UnknownSemantics, InvalidNumber, UnknownShape, InvalidToken,
    DuplicateProperty, MissingProperty, AmbiguousTake, ProcessingNotImplemented,
    SourceNotImplemented
};
struct ImportObject {
    // Ordinal is stable for these exact source bytes, not a destination UUID.
    std::size_t node = ReaperStructureNode::noParent;
    std::size_t parent = ReaperStructureNode::noParent;
    ImportObjectKind kind = ImportObjectKind::Project;
    ForeignByteRange sourceType; // Raw source kind, never loaded.
    bool singleTake = true; // False means take-property selection is unverified.
};
struct ImportProperty {
    std::size_t object = 0, node = ReaperStructureNode::noParent;
    ImportPropertyId id = ImportPropertyId::ProjectSampleRate;
    ImportValueKind kind = ImportValueKind::None;
    ImportEvidenceStatus status = ImportEvidenceStatus::Unverified;
    ImportEvidenceReason reason = ImportEvidenceReason::UnknownSemantics;
    ForeignByteRange value; // Exact token contents, no escape/path/encoding conversion.
    double number = 0; // Read only when kind == Number; no implicit defaults.
};
struct ImportLineEvidence {
    ImportEvidenceStatus status = ImportEvidenceStatus::Unverified;
    ImportEvidenceReason reason = ImportEvidenceReason::UnknownSemantics;
};
struct ReaperImportLimits {
    ReaperStructureLimits structure;
    std::size_t maximumObjects = 100000, maximumProperties = 1000000;
    std::size_t maximumTokensPerMappedLine = 32;
};
// Framework independent, off audio, uniquely owned intermediate representation.
// No media/plugin resolution, session mutation, GUI or foreign code execution.
// Exact opaque line/extent ranges survive, including every unhandled field.
class ReaperImportPreview {
  public:
    static constexpr unsigned schemaVersion = 1;
    ReaperImportPreview(ReaperImportPreview &&) noexcept = default;
    ReaperImportPreview &operator=(ReaperImportPreview &&) = delete;
    ReaperImportPreview(const ReaperImportPreview &) = delete;
    ReaperImportPreview &operator=(const ReaperImportPreview &) = delete;
    const ReaperStructure &structure() const noexcept { return structure_; }
    std::span<const ImportObject> objects() const noexcept { return objects_; }
    std::span<const ImportProperty> properties() const noexcept { return properties_; }
    std::span<const ImportLineEvidence> lines() const noexcept { return lines_; }
    // Returns null for missing ID or duplicate occurrences. A Missing record is
    // returned for a required field absent from the source, with kind None.
    const ImportProperty *property(std::size_t object, ImportPropertyId) const noexcept;
    std::size_t chargedBytes() const noexcept { return lease_.bytes()+structure_.chargedBytes(); }
  private:
    friend ReaperImportPreview inspectReaperImport(std::string_view, ReaperImportLimits,
                                                  ResourceLedger, std::stop_token);
    explicit ReaperImportPreview(ReaperStructure &&s) : structure_(std::move(s)) {}
    ResourceLease lease_; // Retires AFTER all banks and the source owner.
    ReaperStructure structure_;
    std::vector<ImportObject> objects_;
    std::vector<ImportProperty> properties_;
    std::vector<ImportLineEvidence> lines_;
};
ReaperImportPreview inspectReaperImport(std::string_view, ReaperImportLimits,
                                      ResourceLedger, std::stop_token = {});
} // namespace soundcurrent::daw
