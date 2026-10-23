// Windows launcher for CakeStudio: starts the Linux build inside WSL2 (WSLg
// provides the display) without leaving a console window open.
//
// Build with the .NET Framework compiler that ships with Windows:
//   C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /nologo /target:winexe
//     /win32icon:CakeStudio.ico /out:CakeStudio.exe CakeStudioLauncher.cs
//
// The app path inside WSL defaults to ~/CFD_development/build/src/gui/CFDStudio
// and can be overridden with the CAKESTUDIO_PATH environment variable (a Linux path).

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

        string appPath = Environment.GetEnvironmentVariable("CAKESTUDIO_PATH");
        if (string.IsNullOrEmpty(appPath))
            appPath = "$HOME/CFD_development/build/src/gui/CFDStudio";

        // Report a missing binary with exit code 127 so we can explain it below.
        // The app's own output goes to a log file: once this launcher exits its
        // stderr pipe closes, and writing to it would kill the app with SIGPIPE.
        string script = "app=\"" + appPath + "\"; " +
                        "[ -x \"$app\" ] || { echo \"Not found: $app\" >&2; exit 127; }; " +
                        "export DISPLAY=\"${DISPLAY:-:0}\"; " +
                        "log=\"${XDG_STATE_HOME:-$HOME/.local/state}/cakestudio\"; mkdir -p \"$log\"; " +
                        "exec \"$app\" >\"$log/launcher.log\" 2>&1 </dev/null";

        var psi = new ProcessStartInfo
        {
            FileName = wsl,
            // Windows argument parsing only understands double quotes, escaped as \"
            Arguments = "-e bash -lc \"" + script.Replace("\"", "\\\"") + "\"",
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
