# NookBit rule-tree v3: AI authoring contract

Standalone authoring contract for an LLM generating `nookbit_rules.json`. No other document is required. Output **one JSON object only**: no Markdown fence, comments, explanations, UI graph coordinates, or extra fields. If the request is ambiguous or unsupported, ask a concise clarification instead of inventing syntax. Import in NookBit validates the result; saving and file-move approval are separate actions.

## Output shape

The top-level object must contain exactly `schemaVersion`, `ruleSetId`, `version`, and `nodes`, plus optional `globalOtherwise`. Use `"schemaVersion": 3`. `ruleSetId` and every node `id` must match `[A-Za-z0-9._-]+`; node IDs must be unique throughout the entire tree. `version` is an ISO 8601 date-time with a timezone, such as `2026-10-01T00:00:00Z`. `nodes` is a nonempty array of root nodes. Preserve array order: it is rule priority.

Every node must contain `id` and a Boolean `enabled`. An ordinary node must contain exactly one `when` condition. An else node has `"otherwise": true` instead of `when`; at most one else node is allowed in each sibling array. Put it last for readability. A node either has a nonempty `children` array, is a leaf with `destination`, or is a leaf with `"exclude": true`. An internal node must not have `destination` or `exclude`. A leaf must not omit its terminal action, even if `globalOtherwise` exists. Do not add unlisted fields.

Supported `when` objects (use exactly the keys shown for the selected type):

| Purpose | Object | Notes |
| --- | --- | --- |
| Extension | `{"type":"extension","values":["png","jpg"]}` | Nonempty, unique alphanumeric extensions without leading dots; lower-case is recommended. |
| Filename contains text | `{"type":"nameContains","text":"Screenshot"}` | Nonempty substring of the full filename, including its extension; ASCII letters are case-insensitive. |
| File size | `{"type":"size","comparison":"greaterThan","bytes":5000000}` | Integer bytes from 0 to 9007199254740991. Comparisons: `greaterThan`, `atLeast`, `lessThan`, `atMost`. KB=1000, MB=1000000, GB=1000000000; MiB=1048576. Match the user's units exactly. |
| Time since modification | `{"type":"modifiedAgeDays","comparison":"atLeast","days":30}` | Nonnegative whole days; comparisons: `atLeast`, `lessThan`. This is modification time, not creation time. |

For each destination use `{"base":"home","path":"Pictures/{yyyy}/{MM}","collisionPolicy":"inherit"}`. `base` is optional and defaults to `home` for older rules. In `home` mode, `path` is nonempty and relative to the user's configured organizing root; never use a drive letter or absolute path. For a user-requested separate destination use `{"base":"absolute","path":"D:/ImportantFiles","collisionPolicy":"inherit"}` with a fully qualified local folder path. Do not invent machine-specific absolute folders unless the user supplies them. A rule JSON NEVER grants permission to external folders: the user must separately allow the folder in the app before preview/execution. Never use `..` segments in either mode. The only supported date tokens are `{yyyy}` and `{MM}`, expanded from the file's modification date. `collisionPolicy` is required and is one of `inherit`, `rename`, `skip`, `abort`. `inherit` uses the user's configured default. If no date folders are wanted, use a plain path such as `Pictures`.

`globalOtherwise`, if present, is a destination object of the same shape. It applies only when traversal finds neither a destination nor an explicit exclusion. If omitted, an unmatched file remains unclassified in the pending list. `"exclude": true` is an explicit do-not-move result, not a destination named “Unclassified”; it takes precedence over `globalOtherwise`.

## Evaluation rules

At each level, NookBit checks enabled ordinary siblings in array order and selects the **first** matching condition. If none matches, it selects the enabled `otherwise` sibling, if any. It then repeats this process among the selected node's children. Parent and child conditions therefore compose as AND. Once a sibling is selected, a miss among its children does **not** retry later siblings of the parent. A local `otherwise` child can catch that miss. If no child matches and `globalOtherwise` is absent, the file remains unclassified. Disabled nodes are ignored.

Put more specific or higher-priority rules before broad ones. For example, a root “over 20 MiB” rule before a root “PNG/JPG” rule sends a 25 MiB PNG to the large-files destination. To express “PNG/JPG over 5 MiB,” use an extension node with a size child (or the reverse), not two conditions inside one node. Do not invent unsupported conditions such as creation date, regex, MIME type, or arbitrary expressions.

## Complete example

A file over 20 MiB is handled first; smaller PNG/JPG screenshots go to `Pictures/Screenshots`, other PNG/JPG files to `Pictures/Other`, and TMP files are explicitly excluded. Other files remain unclassified.

```json
{
  "schemaVersion": 3,
  "ruleSetId": "ai-example",
  "version": "2026-10-01T00:00:00Z",
  "nodes": [
    {
      "id": "large-files",
      "enabled": true,
      "when": { "type": "size", "comparison": "greaterThan", "bytes": 20971520 },
      "destination": { "path": "LargeFiles", "collisionPolicy": "inherit" }
    },
    {
      "id": "pictures",
      "enabled": true,
      "when": { "type": "extension", "values": ["png", "jpg"] },
      "children": [
        {
          "id": "screenshots",
          "enabled": true,
          "when": { "type": "nameContains", "text": "Screenshot" },
          "destination": { "path": "Pictures/Screenshots", "collisionPolicy": "rename" }
        },
        {
          "id": "other-pictures",
          "enabled": true,
          "otherwise": true,
          "destination": { "path": "Pictures/Other", "collisionPolicy": "inherit" }
        }
      ]
    },
    {
      "id": "ignore-temp",
      "enabled": true,
      "when": { "type": "extension", "values": ["tmp"] },
      "exclude": true
    }
  ]
}
```

## Prompt to pair with this contract

> Create one NookBit rule-tree v3 JSON object for the classification requirements below. Follow the attached authoring contract exactly. Choose a clear root and sibling order, one condition per node, unique IDs, and an explicit destination or exclusion at every leaf. Do not invent conditions. If a requirement cannot be expressed, ask about it before producing JSON. Output JSON only. Requirements: [describe desired classification here]

The app checks JSON syntax, Schema constraints, and additional tree/editor constraints on import. A successful import replaces only the **unsaved editor draft**; the user must choose **Save All** to make it active. Importing or saving rules does not move files. Preview and file-operation approval remain separate steps.

## Final checks before output

- IDs: unique node IDs; only supported fields; booleans and integers are not strings.
- One condition type per node; AND is nesting; extension alternatives belong in one `values` array.
- Each node has exactly one terminal destination/exclusion or nonempty children. No cycle, shared child, or destination-only node without a condition/otherwise wrapper.
- First matching enabled sibling wins; no backtracking. Order is per parent, not global depth or screen position.
- `exclude` leaves files in place; unmatched also leaves files in place unless `globalOtherwise` exists. Never model either as a folder called Unclassified.
- `otherwise` has no `when`; at most one per sibling array. `globalOtherwise` is a destination object, not a node.
- All destinations declare a valid collision policy. Prefer `base: "home"`; `home` is the user's NookBit organizing root, not automatically the OS user directory. Do not substitute a guessed actual home.
- No `..`, drive-relative `D:folder`, unsupported variables such as `${home}`, or unsupported date tokens. Use forward slashes; escape backslashes if unavoidable.
- Do not treat `absolute` as a permission grant. Avoid OS/application-data folders and drive-root destinations. Ask for an explicit external folder if needed.
- No creation-date, wildcard/regex name conditions, MIME/category predicates, scripts, or invented logical operators. Modified age counts completed days (future timestamps count as age zero).
