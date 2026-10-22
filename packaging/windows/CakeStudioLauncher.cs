using System;
using System.Diagnostics;
using System.IO;
using System.Windows.Forms;

static class CakeStudioLauncher
{
    const string Title = "CakeStudio";

    [STAThread]
    static int Main()
    {
        string wsl = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "wsl.exe");
        if (!File.Exists(wsl))
        {
            MessageBox.Show("WSL is not installed. CakeStudio runs inside WSL2.\n\n" +
                            "Install it from an administrator terminal with: wsl --install",
                            Title, MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }

        // Prefer the copy installed by Setup, then a source build in ~/CFD_development
        string appPath = Environment.GetEnvironmentVariable("CAKESTUDIO_PATH");
        string pick = string.IsNullOrEmpty(appPath)
            ? "app=\"$HOME/.local/share/cakestudio/current/bin/CFDStudio\"; " +
              "[ -x \"$app\" ] || app=\"$HOME/CFD_development/build/src/gui/CFDStudio\"; "
            : "app=\"" + appPath + "\"; ";

        // Setup records which WSL distribution it installed into
        string distroArg = "";
        string distroFile = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "CakeStudio.distro");
        if (File.Exists(distroFile))
        {
            string distro = File.ReadAllText(distroFile).Trim();
            if (distro.Length > 0) distroArg = "-d " + distro + " ";
        }

        // Exit 127 flags a missing binary. App output goes to a log because our stderr pipe closes
        // when the launcher exits, and writing to it would kill the app with SIGPIPE.
        string script = pick +
                        "[ -x \"$app\" ] || { echo \"Not found: $app\" >&2; exit 127; }; " +
                        "export DISPLAY=\"${DISPLAY:-:0}\"; " +
                        "log=\"${XDG_STATE_HOME:-$HOME/.local/state}/cakestudio\"; mkdir -p \"$log\"; " +
                        "exec \"$app\" >\"$log/launcher.log\" 2>&1 </dev/null";

        var psi = new ProcessStartInfo
        {
            FileName = wsl,
            // Windows argument parsing only understands double quotes, escaped as \"
            Arguments = distroArg + "-e bash -lc \"" + script.Replace("\"", "\\\"") + "\"",
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardError = true,
        };

        Process proc;
        try
        {
            proc = Process.Start(psi);
        }
        catch (Exception e)
        {
            MessageBox.Show("Could not start WSL:\n" + e.Message, Title,
                            MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }

        // A failure to launch shows up within a few seconds; after that the app
        // is running and the launcher can exit while the app keeps going
        var stderr = proc.StandardError.ReadToEndAsync();
        if (proc.WaitForExit(5000) && proc.ExitCode != 0)
        {
            string detail = stderr.Wait(1000) ? stderr.Result.Trim() : "";
            string msg = proc.ExitCode == 127
                ? "CakeStudio was not found inside WSL.\n\n" + detail +
                  "\n\nBuild it first, or set CAKESTUDIO_PATH to the CFDStudio binary."
                : "CakeStudio exited with code " + proc.ExitCode + ".\n\n" + detail;
            MessageBox.Show(msg, Title, MessageBoxButtons.OK, MessageBoxIcon.Error);
            return proc.ExitCode;
        }
        return 0;
    }
}
