#!/usr/bin/env bash
#
# check-msi - judge a built MSI against the zip it was built from.
#
#   scripts/check-msi.sh dist/cryo-<ver>-windows-x86_64.msi dist/cryo-<ver>-windows-x86_64.zip
#
# Refuses (exit 1, one `::error::` line per finding) an MSI that:
#   - holds files other than the zip's;
#   - lacks the actions that add and remove the PATH entry, or the
#     dependency step;
#   - removes the PATH entry anywhere but inside the install transaction,
#     before the files go, or removes an older Cryo after the transaction
#     opened;
#   - runs the dependency step (or anything else that runs
#     install-dependencies.ps1) in the install's own sequence: actions there
#     are started by the Windows Installer service, with its environment,
#     which holds neither the user's PATH nor their winget, so the step
#     fails on every machine whose toolchain is on the user's PATH.  It
#     belongs in the wizard's sequence, after the files are in place, as the
#     script action that leaves its outcome to the finish page;
#   - has no wizard: no license, location, toolchain or finish page, or a
#     finish page that does not show the dependency step's report.
#
# Needs msiinfo, msiextract (Debian/Ubuntu: msitools) and unzip.

set -euo pipefail

MSI="${1:-}"; ZIP="${2:-}"
[ -f "$MSI" ] && [ -f "$ZIP" ] || { echo "usage: check-msi.sh <msi> <zip>" >&2; exit 2; }
for tool in msiinfo msiextract unzip; do
    command -v "$tool" >/dev/null 2>&1 || { echo "error: ${tool} is required" >&2; exit 2; }
done

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
failed=0
refuse() { echo "::error::$1"; failed=1; }

# A table's rows, tab-separated, without the three header lines and the
# carriage return msiinfo ends each row with; nothing for a table the MSI
# does not have.
table() { { msiinfo export "$MSI" "$1" 2>/dev/null || true; } | tail -n +4 | tr -d '\r'; }
# The sequence number of action $2 in sequence table $1, or nothing.
at() { table "$1" | awk -F'\t' -v action="$2" '$1 == action { print $3 }'; }

unzip -q "$ZIP" -d "${work}/zip"
msiextract -C "${work}/msi" "$MSI" >/dev/null
( cd "${work}"/zip/cryo-* && find . -type f | sort ) > "${work}/zip.files"
( cd "${work}/msi/.cryo" && find . -type f | sort ) > "${work}/msi.files"
diff "${work}/zip.files" "${work}/msi.files" >/dev/null || refuse "the MSI's files differ from the zip's"

actions="$(table CustomAction)"
for action in AddToPath RemoveFromPath DependencyStep; do
    printf '%s\n' "$actions" | awk -F'\t' -v action="$action" '$1 == action { found = 1 } END { exit !found }' \
        || refuse "the MSI has no ${action} action"
done

initialize="$(at InstallExecuteSequence InstallInitialize)"
remove_files="$(at InstallExecuteSequence RemoveFiles)"
remove_from_path="$(at InstallExecuteSequence RemoveFromPath)"
remove_existing="$(at InstallExecuteSequence RemoveExistingProducts)"
[ -n "$remove_from_path" ] && [ "$initialize" -lt "$remove_from_path" ] && [ "$remove_from_path" -lt "$remove_files" ] \
    || refuse "the MSI runs RemoveFromPath at '${remove_from_path}', not between InstallInitialize and RemoveFiles"
[ -n "$remove_existing" ] && [ "$remove_existing" -lt "$initialize" ] \
    || refuse "the MSI runs RemoveExistingProducts at '${remove_existing}', not before InstallInitialize"

# The dependency step: in the wizard, after ExecuteAction, a script action
# stored in the Binary table (type 5 in its low six bits), and
# nothing in the install's own sequence that runs the step's script.
executed="$(at InstallUISequence ExecuteAction)"
step="$(at InstallUISequence DependencyStep)"
[ -n "$step" ] && [ "$step" -gt "$executed" ] \
    || refuse "the MSI's wizard runs no DependencyStep after ExecuteAction (at '${step}')"
[ -z "$(at InstallExecuteSequence DependencyStep)" ] \
    || refuse "the MSI runs DependencyStep in InstallExecuteSequence, with the Windows Installer service's environment"
step_type="$(printf '%s\n' "$actions" | awk -F'\t' '$1 == "DependencyStep" { print $2 }')"
[ "$((step_type & 63))" -eq 5 ] \
    || refuse "the MSI's DependencyStep is of type ${step_type}, not a script stored in the Binary table (5)"
for action in $(table InstallExecuteSequence | cut -f1); do
    printf '%s\n' "$actions" | awk -F'\t' -v action="$action" '$1 == action && $4 ~ /install-dependencies/ { found = 1 } END { exit !found }' \
        && refuse "the MSI's InstallExecuteSequence action ${action} runs install-dependencies.ps1"
done

dialogs="$(table Dialog | cut -f1)"
for dialog in WelcomeDlg CryoLicenseDlg CryoInstallDirDlg CryoToolchainDlg VerifyReadyDlg ProgressDlg CryoExitDlg CryoFatalErrorDlg; do
    printf '%s\n' "$dialogs" | grep -qx "$dialog" || refuse "the MSI's wizard has no ${dialog}"
done
[ "$(at InstallUISequence CryoExitDlg)" = "-1" ] || refuse "the MSI's wizard does not end a successful install on CryoExitDlg"
[ "$(at InstallUISequence CryoFatalErrorDlg)" = "-3" ] || refuse "the MSI's wizard does not end a failed install on CryoFatalErrorDlg"
table Control | awk -F'\t' '$1 == "CryoExitDlg" && $10 ~ /\[CRYO_DEPENDENCY_REPORT\]/ { found = 1 } END { exit !found }' \
    || refuse "the MSI's finish page does not show the dependency step's report"

[ "$failed" -eq 0 ] || exit 1
echo "check-msi: OK -- $(basename "$MSI")"
