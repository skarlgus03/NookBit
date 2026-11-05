# Editable named processing flows

The rule canvas stores real processing nodes and ordered links per named region.
The built-in folder preset is only a template: folder input -> recursive file
traversal -> classifier call. Users can delete, reconnect or replace these steps.
A folder flow without traversal classifies the original folder as one bundle.
A traversal can lead to custom conditions/destinations instead of default rules.
An item-input flow can be called by other flows to reuse a classifier.

## Editing

- Context menu -> folder contents flow creates the basic three-step template.
- Separate classification flow creates an item-input region with a default call.
- Click a region title or one of its nodes to choose the active editing region.
  New conditions, destinations and processing blocks belong to that region.
  Moving a node geometrically does not change its owner.
- Select an input node to edit the name and folder/item input kind. Only folder
  entry points appear in the queue's selector. Whole-folder mode stays the default.
- Select a traversal to choose immediate files, all subfolders, or a limited
  number of subfolder levels. Level 1 includes immediate subfolders. No mode
  emits directories. Select a call to choose default rules or another named flow.
- Delete individual steps to customize the graph; deleting an input removes its
  region. Invalid or missing references are errors, not silent fallback choices.
- Save All validates and publishes the graph and its layout. Import/export uses
  the same JSON validation. Calls replace cross-region wires.

## Ownership

The Qt-independent processing model contains IDs, node kinds, successors and
predicate/destination/call settings. Layout stays separate. RuleEngine implements
`IProcessingFlowRouter` to interpret metadata without filesystem access. The
preview use case requests actual traversal through IScanner, then Planner uses
the same pure interpreter for each resulting source. No arbitrary user code runs.

A selected graph's traversal points become `OperationPlan::processingSources`.
They are included in the approval digest, stored atomically as the single
`processing_inputs_v1` event in the existing audit log, and reloaded for approval
and recovery. No database schema migration is required. Missing or altered context
changes the approval hash; malformed/duplicate context audits are rejected.
Freeze and verification restore the saved contexts on the exact preview sources;
new files are never silently added to an approved operation. Original directories
remain after enumeration, and undo uses each recorded original path.

Existing path-only pending documents and `recursiveFilesToDefault` presets remain
readable. Presets are expanded to actual nodes when edited. Legacy rule and plan
signatures remain unchanged when they do not contain graph/context extensions.

## Validation and extension

Graphs are bounded to 24 regions, 1000 processing nodes and depth 32. Every node
is reachable with one parent. Branches retain ordered first-match/no-backtracking
semantics. Conditions reuse the normal atomic predicate validator. Node cycles,
recursive calls, absent destinations, unknown kinds/targets and file-to-folder
input mismatches are rejected. A traversal requires a folder input or a folder
condition guard; it emits only files. Typed inputs distinguish folder entry
points from reusable item classifiers.

Additional algorithms can be composed from the current primitives. New primitive
node kinds require implementations and matching schema/editor/validation support;
this is not yet a dynamic plugin loader or arbitrary scripting engine. Logical
AND/OR/NOT and semantic content analyzers remain separate future work.

Tests cover editable graph round trips, scoped queue choices, custom PNG-only
classification through another flow, default bundle preservation, signed context
SQLite persistence, actual temporary-file move/undo, type/cycle rejection and QML
rewiring. Automated tests do not use personal download files.
