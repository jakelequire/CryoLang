// The MSI's dependency step, run by the installer's interface once the
// files are in place: the install's install-dependencies.ps1 in a hidden
// window, told by the wizard's checkbox whether it may install a C
// toolchain, its report written to CRYO_DEPENDENCY_LOG.  The outcome is
// left in properties the finish page reads (CRYO_DEPENDENCY_STATUS "ok" or
// "failed", and CRYO_DEPENDENCY_REPORT, the report's last error line), so
// a failure is shown there rather than in a window that closes.  The step
// always reports success to the installer: the files are installed either
// way, and the finish page says what is missing.

function DependencyStep() {
    var log = Session.Property("CRYO_DEPENDENCY_LOG");
    try {
        var answer = Session.Property("CRYO_INSTALL_TOOLCHAIN") == "1" ? "-Yes" : "-No";
        var command = "\"" + Session.Property("POWERSHELL") + "\" -NoProfile -NonInteractive"
            + " -ExecutionPolicy Bypass -File \"" + Session.Property("INSTALLDIR")
            + "install-dependencies.ps1\" " + answer + " -Log \"" + log + "\"";
        var status = new ActiveXObject("WScript.Shell").Run(command, 0, true);
        if (status == 0) {
            Session.Property("CRYO_DEPENDENCY_STATUS") = "ok";
        } else {
            Session.Property("CRYO_DEPENDENCY_STATUS") = "failed";
            Session.Property("CRYO_DEPENDENCY_REPORT") = LastError(log, status);
        }
    } catch (error) {
        Session.Property("CRYO_DEPENDENCY_STATUS") = "failed";
        Session.Property("CRYO_DEPENDENCY_REPORT") = "the dependency step could not be run: " + error.message;
    }
    return 1;
}

// The last line of the report that states an error, without its label, or
// the exit status when the report holds none.
function LastError(log, status) {
    var found = "the dependency step exited " + status + " without a report";
    var files = new ActiveXObject("Scripting.FileSystemObject");
    if (!files.FileExists(log)) { return found; }
    var report = files.OpenTextFile(log, 1, false, 0);
    while (!report.AtEndOfStream) {
        var line = report.ReadLine();
        var at = line.indexOf("[error] ");
        if (at >= 0) { found = line.substring(at + 8); }
    }
    report.Close();
    return found;
}
