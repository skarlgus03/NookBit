# Initial setup

The host opens its initial-setup page until `onboarding/completed` is explicitly
saved by Start or Skip. This also applies once to existing installations without
that marker; their current destination, rules and companion visibility are
prefilled. Closing/minimizing the application does not count as completion.

- Start validates the destination before calling any feature. A missing output
  folder may be selected, but setup does not create it or move files.
- Keep rules preserves the active rules unchanged. On a fresh installation these
  are the normal bundled defaults already prepared by File Organizer.
- Import runs the existing v3 JSON/graph validation and saves only a valid ruleset.
  Errors show the reported location/reason and leave setup incomplete. Imported
  external destinations do not gain authorization.
- Companion selection changes desktop visibility, not module enablement. Disabled
  features are omitted, and setup never constructs substitute feature runtimes.
- Skip never invokes feature configuration and only records completion.
- Settings → Information → **처음 설정 다시 보기** reopens the page with live
  destination, rules and companion visibility. Each opening discards old page
  drafts and resets the rule choice to Keep. Close (including the header/window
  close control) returns to Settings without applying changes. Apply and Close
  uses the same validation/configuration path. Neither action changes the existing
  completion marker, so reviewing does not cause another automatic first launch.
- Rule import rejects unsaved rule-controller drafts rather than replacing them.
- The completion marker is written after successful application. A preference
  storage error keeps the page open; already applied feature settings may remain
  and the user can retry. This is not a transaction across rules and preferences.

`InitialSetupController` owns the input/completion flow and uses an injected apply
callback. Main composes existing feature capabilities; File Organizer owns its
rule import and default-destination update. The page receives only this controller
and shared UI roles. Normal UI verification disables setup and preference writes.

Tests cover validation, skip, application errors, completion save errors and
restoration with temporary INI stores. `--verify-initial-setup` and
`--verify-initial-setup-skip` additionally exercise actual QML buttons, page
navigation, review/cancel/reapply, live destination binding and completion restore in isolated app data.
Neither verification mode uses the user's preferences or moves their files.
