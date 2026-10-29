# History search

The History page searches all stored execution records, not just the most recent
50 loaded rows. Search and filters change only the read view. They do not execute,
approve, retry or undo file operations.

## User controls

- Text: literal substring of the original path or destination path, including
  either filename. English letter case is ignored. `/` and `\` are equivalent;
  Korean text, spaces and punctuation remain searchable. `%`, `_` and quotes
  have no special search syntax. Surrounding whitespace is trimmed.
- Period: all, today, the last 7 calendar days, the last 30 calendar days, or an
  inclusive custom `YYYY-MM-DD` range. Presets include today. Dates are local to
  the user's system; the query receives UTC boundaries with an inclusive start
  and exclusive start-of-next-day end. Refresh recalculates relative periods.
- Result: all, move completed, rollback completed, or problems. Problems group
  failed, executing/interrupted, rolling-back/interrupted and needs-recovery plans.
  Rows continue to show the precise status. This is a plan-state filter; it does
  not silently change to a per-file-state filter.
- Order: newest or oldest, using the same last-activity timestamp displayed in
  history. Undo/recovery events can therefore change the displayed date. It is
  not a search for the original move date alone.

Press Search or Enter to apply text/date edits. Choosing a dropdown applies its
current selection; switching to custom dates seeds empty fields with today.
Reset clears text, period and result and restores newest-first ordering.
Invalid ranges and failed reads show a message and preserve the previous applied
query, rows and selection. File work disables the controls.

The first page contains at most 50 matching plans. Load more appends the next 50
without clearing the selected plan. There is no expensive exact-total count query;
the page reports the number currently loaded and whether another page exists.
Existing today/yesterday/date grouping is retained after filtering and sorting.

## Selection and undo

The timeline shows plans containing matching files, with a per-plan match count.
Selecting one always loads all its stored steps. Matching rows are labelled
**검색 일치** and their filename uses the shared accent color. Nonmatching rows
remain visible. Applied/rolled-back counts retain their existing meaning.

The undo button and confirmation show the number of currently applied steps for
the entire selected plan. A search never reduces that scope to matching files.
The existing `IFileOperations::rollback(planId)` and approval/verification behavior
are unchanged. Preview-only/unsent plans are not included in execution history.

## Boundaries and storage

`IHistoryQuery` exposes a standard C++ `HistorySearch` request and
`HistorySearchPage` response. It contains no Qt, widget, filesystem worker or
mutation method. `SqliteHistoryRepository` implements this capability alongside
the existing history persistence interface. The host injects both capabilities
explicitly into HistoryController. Existing `recentPlans(limit)` delegates to the
same query with default filters.

SQLite uses bound literal parameters and an existence/count condition over stored
source/destination paths. It returns one summary per plan even when several steps
match. Status strings and sort operators come from validated enums, not text
input. No schema migration or filesystem metadata/content reads are introduced.
The cursor is the last `(activity timestamp, plan ID)` tuple. Both directions use
the same ID tie-breaker; a one-row lookahead decides whether to offer another page.
The controller also deduplicates IDs when appending. Refresh reloads the first
page; pagination is not a permanent snapshot across other processes' writes.

HistoryController converts local dates and statuses into query values, validates
input and exposes applied state. HistorySearchBar owns only input drafts and
signals; HistoryPage forwards them to its explicit controller. HistoryTimeline
and HistoryDetail remain views with inputs and requests. All controls, typography,
spacing and highlight colors use the shared UI components/tokens.

## Verification

Temporary SQLite fixtures cover more than 50 plans, tied timestamps, both cursor
directions, literal special characters, source/destination matches, Korean paths,
case-insensitive English, status categories and last-event date boundaries.
Controller checks cover local-date conversion, combined filters, invalid ranges,
query failure, selected-detail preservation and full-plan undo despite a one-file
match. QML boundary tests cover search, dropdowns, reset and load-more routing,
work-time input blocking and existing overlay/summary behavior. Existing actual
move/undo, runtime-lifetime and UI smoke tests remain part of validation.
