using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Reflection;
using System.Text;
using System.Threading;
using System.Windows.Forms;
using Microsoft.Win32;

static class Product
{
    public const string Name = "CakeStudio";
    // Set at build time from the release tag (build-setup.cmd embeds VERSION.txt)
    static string version;
    public static string Version
    {
        get
        {
            if (version == null)
            {
                Stream s = Resource("VERSION.txt");
                version = s == null ? "0.0.0" : new StreamReader(s).ReadToEnd().Trim();
            }
            return version;
        }
    }
    public const string Publisher = "CakeCFD";
    public const string Url = "https://github.com/CakeCFD/cake-studio";
    public const string UbuntuDistro = "Ubuntu-24.04";

    public static string InstallDir
    {
        get
        {
            return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                                "Programs", Name);
        }
    }
    public const string StateKey = @"Software\CakeCFD\CakeStudio\Setup";
    public const string UninstallKey = @"Software\Microsoft\Windows\CurrentVersion\Uninstall\CakeStudio";
    public const string RunOnceKey = @"Software\Microsoft\Windows\CurrentVersion\RunOnce";

    public static Stream Resource(string name)
    {
        return Assembly.GetExecutingAssembly().GetManifestResourceStream(name);
    }
    public static string ResourceText(string name)
    {
        using (var r = new StreamReader(Resource(name), new UTF8Encoding(false)))
            return r.ReadToEnd();
    }
    public static Image ResourceImage(string name)
    {
        return Image.FromStream(Resource(name));
    }
}

// Runs wsl.exe and streams its output line by line
static class Wsl
{
    public static string Exe
    {
        get
        {
            string dir = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
            // A 32-bit process on 64-bit Windows must go through Sysnative to see wsl.exe
            if (!Environment.Is64BitProcess && Environment.Is64BitOperatingSystem)
                return Path.Combine(dir, @"Sysnative\wsl.exe");
            return Path.Combine(dir, @"System32\wsl.exe");
        }
    }

    public static Process Current;

    public static int Run(string args, string stdin, Action<string> onLine)
    {
        var psi = new ProcessStartInfo(Exe, args);
        psi.UseShellExecute = false;
        psi.CreateNoWindow = true;
        psi.RedirectStandardOutput = true;
        psi.RedirectStandardError = true;
        psi.RedirectStandardInput = stdin != null;
        psi.StandardOutputEncoding = Encoding.UTF8;
        psi.StandardErrorEncoding = Encoding.UTF8;
        psi.EnvironmentVariables["WSL_UTF8"] = "1";
        using (var p = new Process())
        {
            p.StartInfo = psi;
            DataReceivedEventHandler h = delegate(object s, DataReceivedEventArgs e)
            {
                if (e.Data != null && onLine != null) onLine(e.Data.Replace("\0", "").TrimEnd('\r'));
            };
            p.OutputDataReceived += h;
            p.ErrorDataReceived += h;
            p.Start();
            Current = p;
            p.BeginOutputReadLine();
            p.BeginErrorReadLine();
            if (stdin != null)
            {
                // bash reads the script from stdin, so it needs LF endings and no BOM
                var w = new StreamWriter(p.StandardInput.BaseStream, new UTF8Encoding(false));
                w.NewLine = "\n";
                w.Write(stdin.Replace("\r\n", "\n"));
                w.Close();
            }
            p.WaitForExit();
            Current = null;
            return p.ExitCode;
        }
    }

    public static string Capture(string args)
    {
        var sb = new StringBuilder();
        try { Run(args, null, delegate(string l) { lock (sb) sb.AppendLine(l); }); }
        catch (Exception) { return null; }
        return sb.ToString();
    }

    public static bool IsInstalled()
    {
        if (!File.Exists(Exe)) return false;
        try { return Run("--version", null, null) == 0; }
        catch (Exception) { return false; }
    }

    // Name of a WSL2 Ubuntu 24.04 distribution, preferring the default one
    public static string FindUbuntu2404()
    {
        string list = Capture("-l -v");
        if (list == null) return null;
        var names = new List<string>();
        foreach (string raw in list.Split('\n'))
        {
            string line = raw.Trim();
            if (line.Length == 0 || line.StartsWith("NAME")) continue;
            bool isDefault = line.StartsWith("*");
            string[] parts = line.TrimStart('*').Trim().Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length < 3 || parts[parts.Length - 1] != "2") continue;
            string name = string.Join(" ", parts, 0, parts.Length - 2);
            if (name.StartsWith("docker-desktop")) continue;
            if (isDefault) names.Insert(0, name); else names.Add(name);
        }
        foreach (string n in names)
        {
            string os = Capture("-d " + n + " -e sh -c \". /etc/os-release && echo $ID:$VERSION_ID\"");
            if (os != null && os.Contains("ubuntu:24.04")) return n;
        }
        return null;
    }
}

class SetupState
{
    public bool Accepted, Teno = true, Desktop = true, StartMenu = true;
    public string Distro;

    public void Save()
    {
        using (var k = Registry.CurrentUser.CreateSubKey(Product.StateKey))
        {
            k.SetValue("Accepted", Accepted ? 1 : 0);
            k.SetValue("Teno", Teno ? 1 : 0);
            k.SetValue("Desktop", Desktop ? 1 : 0);
            k.SetValue("StartMenu", StartMenu ? 1 : 0);
        }
    }

    public static SetupState Load()
    {
        var s = new SetupState();
        using (var k = Registry.CurrentUser.OpenSubKey(Product.StateKey))
        {
            if (k == null) return s;
            s.Accepted = (int)k.GetValue("Accepted", 0) == 1;
            s.Teno = (int)k.GetValue("Teno", 1) == 1;
            s.Desktop = (int)k.GetValue("Desktop", 1) == 1;
            s.StartMenu = (int)k.GetValue("StartMenu", 1) == 1;
        }
        return s;
    }

    public static void Clear()
    {
        Registry.CurrentUser.DeleteSubKeyTree(Product.StateKey, false);
    }
}

class SetupForm : Form
{
    enum PageId { Welcome, License, Linux, Components, Ready, Installing, Restart, Finish }

    // Classic wizard metrics at 96 DPI
    const int W = 497, H = 360, TopH = 313, HeaderH = 57;

    readonly SetupState state;
    readonly bool resuming;
    PageId page;
    bool wslInstalled, installDone, installFailed, cancelRequested;

    Panel topArea;
    readonly Dictionary<PageId, Panel> pages = new Dictionary<PageId, Panel>();
    Button backBtn, nextBtn, cancelBtn;

    // Page widgets
    RadioButton acceptRadio, declineRadio;
    Label wslStatus, distroStatus, linuxNote;
    CheckBox installWslCheck, tenoCheck, desktopCheck, startMenuCheck;
    TextBox readyText;
    Label installStatus;
    ProgressBar progress;
    Button detailsBtn;
    ListBox details;
    RadioButton restartNow, restartLater;
    CheckBox launchCheck;
    Label finishText;

    Font baseFont = new Font("Tahoma", 8.25f);
    Font boldFont = new Font("Tahoma", 8.25f, FontStyle.Bold);
    Font titleFont = new Font("Tahoma", 12f, FontStyle.Bold);

    public SetupForm(SetupState st, bool resume)
    {
        state = st;
        resuming = resume;
        SuspendLayout();
        AutoScaleDimensions = new SizeF(96f, 96f);
        AutoScaleMode = AutoScaleMode.Dpi;
        Font = baseFont;
        Text = Product.Name + " " + Product.Version + " Setup";
        ClientSize = new Size(W, H);
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false;
        MinimizeBox = false;
        StartPosition = FormStartPosition.CenterScreen;
        BackColor = SystemColors.Control;
        try { Icon = new Icon(Product.Resource("CakeStudio.ico")); } catch (Exception) { }

        topArea = new Panel { Bounds = new Rectangle(0, 0, W, TopH) };
        Controls.Add(topArea);
        Controls.Add(Etched(0, TopH, W));
        var brand = new Label
        {
            Text = Product.Name + " Setup " + Product.Version,
            ForeColor = SystemColors.GrayText,
            AutoSize = true,
            Location = new Point(10, 330),
        };
        Controls.Add(brand);
        brand.BringToFront();

        backBtn = Btn("< &Back", 247, delegate { Go(Prev(page)); });
        nextBtn = Btn("&Next >", 322, delegate { OnNext(); });
        cancelBtn = Btn("Cancel", 410, delegate { Close(); });
        AcceptButton = nextBtn;

        BuildWelcome();
        BuildLicense();
        BuildLinux();
        BuildComponents();
        BuildReady();
        BuildInstalling();
        BuildRestart();
        BuildFinish();
        ResumeLayout(false);
        page = PageId.Welcome;
        foreach (var kv in pages) kv.Value.Visible = kv.Key == PageId.Welcome;
        UpdateButtons();
    }

    protected override void OnShown(EventArgs e)
    {
        base.OnShown(e);
        if (resuming) Go(PageId.Installing);
    }

    // Layout helpers

    static Label Etched(int x, int y, int w)
    {
        return new Label { BorderStyle = BorderStyle.Fixed3D, Bounds = new Rectangle(x, y, w, 2) };
    }

    Button Btn(string text, int x, EventHandler click)
    {
        var b = new Button { Text = text, Bounds = new Rectangle(x, 325, 75, 23), FlatStyle = FlatStyle.System };
        b.Click += click;
        Controls.Add(b);
        return b;
    }

    // Welcome and Finish pages have the banner on the left and a white text panel on the right
    Panel BannerPage(PageId id, string title, out Panel body)
    {
        var p = new Panel { Bounds = new Rectangle(0, 0, W, TopH), BackColor = Color.White, Visible = false };
        var pic = new PictureBox
        {
            Bounds = new Rectangle(0, 0, 164, TopH),
            SizeMode = PictureBoxSizeMode.StretchImage,
            Image = Product.ResourceImage("banner.png"),
        };
        p.Controls.Add(pic);
        p.Controls.Add(new Label
        {
            Text = title,
            Font = titleFont,
            Bounds = new Rectangle(176, 10, 305, 48),
            BackColor = Color.White,
        });
        body = new Panel { Bounds = new Rectangle(176, 62, 305, 245), BackColor = Color.White };
        p.Controls.Add(body);
        topArea.Controls.Add(p);
        pages[id] = p;
        return p;
    }

    // Inner pages have a white header with title, subtitle and logo over a grey body
    Panel InnerPage(PageId id, string title, string subtitle)
    {
        var p = new Panel { Bounds = new Rectangle(0, 0, W, TopH), Visible = false };
        var header = new Panel { Bounds = new Rectangle(0, 0, W, HeaderH), BackColor = Color.White };
        header.Controls.Add(new Label { Text = title, Font = boldFont, AutoSize = true, Location = new Point(10, 10), BackColor = Color.White });
        header.Controls.Add(new Label { Text = subtitle, Bounds = new Rectangle(20, 28, 400, 26), BackColor = Color.White });
        header.Controls.Add(new PictureBox
        {
            Bounds = new Rectangle(W - 55, 4, 49, 49),
            SizeMode = PictureBoxSizeMode.StretchImage,
            Image = Product.ResourceImage("header.png"),
        });
        p.Controls.Add(header);
        p.Controls.Add(Etched(0, HeaderH, W));
        topArea.Controls.Add(p);
        pages[id] = p;
        return p;
    }

    static Label AddLabel(Control parent, string text, int x, int y, int w, int h)
    {
        var l = new Label { Text = text, Bounds = new Rectangle(x, y, w, h), BackColor = Color.Transparent };
        parent.Controls.Add(l);
        return l;
    }

    // Pages

    void BuildWelcome()
    {
        Panel body;
        BannerPage(PageId.Welcome, "Welcome to the " + Product.Name + " Setup Wizard", out body);
        AddLabel(body, "This wizard will guide you through the installation of " + Product.Name + " " + Product.Version +
             ", a desktop environment for computational fluid dynamics built on OpenFOAM.\r\n\r\n" +
             Product.Name + " runs inside the Windows Subsystem for Linux (WSL2). Setup will check for it, " +
             "install it if you agree, and then download OpenFOAM v2412 and " + Product.Name + ".\r\n\r\n" +
             "An internet connection and about 2 GB of free disk space are required.\r\n\r\n" +
             "Click Next to continue.", 0, 0, 305, 240);
    }

    void BuildLicense()
    {
        var p = InnerPage(PageId.License, "License Agreement",
                          "Please review the license terms before installing " + Product.Name + ".");
        AddLabel(p, "Press Page Down to see the rest of the agreement.", 20, 66, 450, 16);
        var box = new TextBox
        {
            Multiline = true,
            ReadOnly = true,
            ScrollBars = ScrollBars.Vertical,
            BackColor = SystemColors.Window,
            Bounds = new Rectangle(20, 84, 457, 150),
            Text = Product.ResourceText("TERMS.txt").Replace("\r\n", "\n").Replace("\n", "\r\n"),
        };
        p.Controls.Add(box);
        AddLabel(p, "If you accept the terms of the agreement, select the first option below. " +
                "You must accept the agreement to install " + Product.Name + ".", 20, 238, 457, 28);
        acceptRadio = new RadioButton { Text = "I &accept the terms of the License Agreement", Bounds = new Rectangle(20, 268, 450, 18) };
        declineRadio = new RadioButton { Text = "I &do not accept the terms of the License Agreement", Bounds = new Rectangle(20, 287, 450, 18), Checked = true };
        acceptRadio.CheckedChanged += delegate { UpdateButtons(); };
        p.Controls.Add(acceptRadio);
        p.Controls.Add(declineRadio);
    }

    void BuildLinux()
    {
        var p = InnerPage(PageId.Linux, "Linux Environment",
                          Product.Name + " runs on Ubuntu 24.04 inside the Windows Subsystem for Linux.");
        AddLabel(p, "Setup checked this computer for the required components:", 20, 70, 450, 16);
        AddLabel(p, "Windows Subsystem for Linux (WSL2):", 36, 96, 220, 16);
        wslStatus = AddLabel(p, "", 260, 96, 210, 16);
        AddLabel(p, "Ubuntu 24.04 distribution:", 36, 116, 220, 16);
        distroStatus = AddLabel(p, "", 260, 116, 210, 16);
        linuxNote = AddLabel(p, "", 20, 148, 457, 90);
        installWslCheck = new CheckBox { Bounds = new Rectangle(20, 248, 457, 34), CheckAlign = ContentAlignment.TopLeft, TextAlign = ContentAlignment.TopLeft };
        installWslCheck.CheckedChanged += delegate { UpdateButtons(); };
        p.Controls.Add(installWslCheck);
    }

    void BuildComponents()
    {
        var p = InnerPage(PageId.Components, "Choose Components",
                          "Choose which features of " + Product.Name + " you want to install.");
        AddLabel(p, "Check the components you want to install and uncheck the components you don't want to install. Click Next to continue.", 20, 66, 457, 30);
        var group = new GroupBox { Text = "Components", Bounds = new Rectangle(20, 100, 457, 150) };
        p.Controls.Add(group);
        int y = 20;
        foreach (string req in new[] { Product.Name + " " + Product.Version + " (required)", "OpenFOAM v2412 and libraries (required)" })
        {
            group.Controls.Add(new CheckBox { Text = req, Checked = true, Enabled = false, Bounds = new Rectangle(14, y, 420, 20) });
            y += 22;
        }
        tenoCheck = new CheckBox { Text = "TENO high-order convection schemes for OpenFOAM", Checked = state.Teno, Bounds = new Rectangle(14, y, 420, 20) };
        desktopCheck = new CheckBox { Text = "Desktop shortcut", Checked = state.Desktop, Bounds = new Rectangle(14, y + 22, 420, 20) };
        startMenuCheck = new CheckBox { Text = "Start menu shortcut", Checked = state.StartMenu, Bounds = new Rectangle(14, y + 44, 420, 20) };
        group.Controls.Add(tenoCheck);
        group.Controls.Add(desktopCheck);
        group.Controls.Add(startMenuCheck);
        AddLabel(p, "Space required: about 2 GB inside Ubuntu, 1 MB on Windows.", 20, 262, 457, 16);
    }

    void BuildReady()
    {
        var p = InnerPage(PageId.Ready, "Ready to Install",
                          "Setup is ready to install " + Product.Name + " on your computer.");
        AddLabel(p, "Click Install to continue, or Back to review or change any settings.", 20, 66, 457, 16);
        readyText = new TextBox
        {
            Multiline = true, ReadOnly = true, BackColor = SystemColors.Window,
            ScrollBars = ScrollBars.Vertical, Bounds = new Rectangle(20, 88, 457, 200),
        };
        p.Controls.Add(readyText);
    }

    void BuildInstalling()
    {
        var p = InnerPage(PageId.Installing, "Installing",
                          "Please wait while " + Product.Name + " is being installed.");
        installStatus = AddLabel(p, "Preparing...", 20, 72, 457, 16);
        progress = new ProgressBar { Bounds = new Rectangle(20, 92, 457, 16), Maximum = 100 };
        p.Controls.Add(progress);
        detailsBtn = new Button { Text = "Show &details", Bounds = new Rectangle(20, 120, 90, 23), FlatStyle = FlatStyle.System };
        details = new ListBox { Bounds = new Rectangle(20, 120, 457, 180), Visible = false, IntegralHeight = false, HorizontalScrollbar = true };
        detailsBtn.Click += delegate { detailsBtn.Visible = false; details.Visible = true; };
        p.Controls.Add(detailsBtn);
        p.Controls.Add(details);
    }

    void BuildRestart()
    {
        Panel body;
        BannerPage(PageId.Restart, "Restart Required", out body);
        AddLabel(body, "The Windows Subsystem for Linux has been enabled. Your computer must be restarted to finish " +
                   "setting it up.\r\n\r\nAfter the restart, " + Product.Name + " Setup will start again automatically " +
                   "and continue where it left off.\r\n\r\nPlease save your work before restarting.", 0, 0, 305, 120);
        restartNow = new RadioButton { Text = "&Restart now", Checked = true, Bounds = new Rectangle(0, 130, 300, 20), BackColor = Color.White };
        restartLater = new RadioButton { Text = "I want to restart &later", Bounds = new Rectangle(0, 152, 300, 20), BackColor = Color.White };
        body.Controls.Add(restartNow);
        body.Controls.Add(restartLater);
    }

    void BuildFinish()
    {
        Panel body;
        BannerPage(PageId.Finish, "Completing the " + Product.Name + " Setup Wizard", out body);
        finishText = AddLabel(body, Product.Name + " has been installed on your computer.\r\n\r\nClick Finish to close Setup.", 0, 0, 305, 56);
        launchCheck = new CheckBox { Text = "&Run " + Product.Name, Checked = true, Bounds = new Rectangle(0, 62, 300, 20), BackColor = Color.White };
        body.Controls.Add(launchCheck);
    }

    // Navigation

    static PageId Prev(PageId p)
    {
        return p == PageId.Welcome ? p : (PageId)((int)p - 1);
    }

    void Go(PageId target)
    {
        page = target;
        foreach (var kv in pages) kv.Value.Visible = kv.Key == target;
        if (target == PageId.Linux) CheckLinux();
        if (target == PageId.Ready) FillReady();
        if (target == PageId.Installing && !installDone) StartInstall();
        UpdateButtons();
    }

    void UpdateButtons()
    {
        backBtn.Visible = page != PageId.Finish && page != PageId.Restart;
        backBtn.Enabled = page > PageId.Welcome && page < PageId.Installing && !resuming;
        cancelBtn.Enabled = page != PageId.Finish && page != PageId.Restart && !installDone;
        nextBtn.Text = page == PageId.Ready ? "&Install"
                     : page == PageId.Finish || page == PageId.Restart ? "&Finish" : "&Next >";
        bool ok = true;
        if (page == PageId.License) ok = acceptRadio.Checked;
        if (page == PageId.Linux) ok = (wslInstalled && state.Distro != null) || installWslCheck.Checked;
        if (page == PageId.Installing) ok = false;
        nextBtn.Enabled = ok;
    }

    void OnNext()
    {
        switch (page)
        {
            case PageId.License: state.Accepted = true; break;
            case PageId.Components:
                state.Teno = tenoCheck.Checked;
                state.Desktop = desktopCheck.Checked;
                state.StartMenu = startMenuCheck.Checked;
                break;
            case PageId.Restart:
                if (restartNow.Checked) Process.Start("shutdown.exe", "/r /t 0");
                Close();
                return;
            case PageId.Finish:
                if (launchCheck.Checked && installDone && !installFailed)
                {
                    string launcher = Path.Combine(Product.InstallDir, "CakeStudio.exe");
                    if (File.Exists(launcher)) Process.Start(launcher);
                }
                Close();
                return;
        }
        Go((PageId)((int)page + 1));
    }

    void CheckLinux()
    {
        wslStatus.Text = "Checking...";
        distroStatus.Text = "";
        Application.DoEvents();
        Cursor = Cursors.WaitCursor;
        wslInstalled = Wsl.IsInstalled();
        state.Distro = wslInstalled ? Wsl.FindUbuntu2404() : null;
        Cursor = Cursors.Default;
        ShowLinuxStatus();
    }

    void ShowLinuxStatus()
    {
        wslStatus.Text = wslInstalled ? "Installed" : "Not installed";
        wslStatus.ForeColor = wslInstalled ? Color.DarkGreen : Color.DarkRed;
        distroStatus.Text = state.Distro != null ? "Found (" + state.Distro + ")" : "Not found";
        distroStatus.ForeColor = state.Distro != null ? Color.DarkGreen : Color.DarkRed;

        if (wslInstalled && state.Distro != null)
        {
            linuxNote.Text = "Everything " + Product.Name + " needs is already in place. Setup will install OpenFOAM and " +
                             Product.Name + " into the \"" + state.Distro + "\" distribution.";
            installWslCheck.Visible = false;
        }
        else if (!wslInstalled)
        {
            linuxNote.Text = "WSL2 is not installed. Setup can enable it for you using Microsoft's own installer " +
                             "(wsl --install). This needs administrator approval and a restart; Setup will continue " +
                             "automatically afterwards.\r\n\r\nYou will then be asked to create a Linux user name and password " +
                             "for Ubuntu. They are only used inside Ubuntu.";
            installWslCheck.Text = "Yes, install WSL2 and Ubuntu 24.04 (administrator approval and a restart are required)";
            installWslCheck.Visible = true;
        }
        else
        {
            linuxNote.Text = "WSL2 is installed but Ubuntu 24.04 was not found. Setup can install it for you. A console " +
                             "window will open where you create a Linux user name and password; type exit there when " +
                             "it shows a command prompt, and Setup will continue.";
            installWslCheck.Text = "Yes, install Ubuntu 24.04";
            installWslCheck.Visible = true;
        }
        UpdateButtons();
    }

    void FillReady()
    {
        var sb = new StringBuilder();
        sb.AppendLine("Linux environment:");
        if (!wslInstalled) sb.AppendLine("      Install WSL2 (restart required), then Ubuntu 24.04");
        else if (state.Distro == null) sb.AppendLine("      Install Ubuntu 24.04");
        else sb.AppendLine("      Use the existing \"" + state.Distro + "\" distribution");
        sb.AppendLine();
        sb.AppendLine("Inside Ubuntu:");
        sb.AppendLine("      OpenFOAM v2412 and required libraries (apt)");
        sb.AppendLine("      " + Product.Name + " " + Product.Version + " (downloaded from GitHub)");
        if (tenoCheck.Checked) sb.AppendLine("      TENO scheme library");
        sb.AppendLine();
        sb.AppendLine("On Windows:");
        sb.AppendLine("      " + Product.InstallDir);
        if (desktopCheck.Checked) sb.AppendLine("      Desktop shortcut");
        if (startMenuCheck.Checked) sb.AppendLine("      Start menu shortcut");
        readyText.Text = sb.ToString();
    }

    // Installation (background thread)

    // Silent mode writes progress to this log
    TextWriter silentLog;

    void Log(string line)
    {
        if (silentLog != null) { lock (silentLog) silentLog.WriteLine(line); return; }
        BeginInvoke((MethodInvoker)delegate
        {
            details.Items.Add(line);
            details.TopIndex = Math.Max(0, details.Items.Count - 1);
        });
    }

    void Status(string text, int pct)
    {
        if (silentLog != null)
        {
            if (text != null) Log("[" + (pct >= 0 ? pct + "%" : "..") + "] " + text);
            return;
        }
        BeginInvoke((MethodInvoker)delegate
        {
            if (text != null) installStatus.Text = text;
            if (pct >= 0) progress.Value = Math.Max(progress.Value, Math.Min(100, pct));
        });
    }

    // Maps the script's @@ markers onto a slice [lo, hi] of the overall bar
    Action<string> ScriptOutput(int lo, int hi, List<string> errors)
    {
        return delegate(string line)
        {
            if (line.StartsWith("@@STEP "))
            {
                Status(line.Substring(7), -1);
                if (silentLog == null) Log(line.Substring(7));   // silent Status already logs it
            }
            else if (line.StartsWith("@@PROGRESS "))
            {
                int p;
                if (int.TryParse(line.Substring(11).Trim(), out p)) Status(null, lo + (hi - lo) * p / 100);
            }
            else if (line.StartsWith("@@ERROR ")) { lock (errors) errors.Add(line.Substring(8)); }
            else if (line.StartsWith("@@DONE")) { }
            else Log(line);
        };
    }

    void StartInstall()
    {
        state.Save();
        var t = new Thread(InstallWorker) { IsBackground = true };
        t.Start();
    }

    void InstallWorker()
    {
        try
        {
            Install();
            installDone = true;
            BeginInvoke((MethodInvoker)delegate { Go(PageId.Finish); });
        }
        catch (RestartNeeded)
        {
            BeginInvoke((MethodInvoker)delegate { installDone = true; Go(PageId.Restart); });
        }
        catch (Exception e)
        {
            if (cancelRequested) return;
            installFailed = true;
            Log("Error: " + e.Message);
            BeginInvoke((MethodInvoker)delegate
            {
                installStatus.Text = "Installation failed.";
                detailsBtn.Visible = false;
                details.Visible = true;
                MessageBox.Show(this, e.Message + "\r\n\r\nSee the details list for the full log.", Text,
                                MessageBoxButtons.OK, MessageBoxIcon.Error);
                installDone = true;
                finishText.Text = Product.Name + " was not installed.\r\n\r\nYou can run Setup again to retry. " +
                                  "If the problem continues, please report it at " + Product.Url + "/issues";
                launchCheck.Visible = false;
                Go(PageId.Finish);
            });
        }
    }

    class RestartNeeded : Exception { }

    // Unattended install with default components; returns a process exit code
    public int RunSilent(string logPath)
    {
        using (silentLog = new StreamWriter(logPath, false, new UTF8Encoding(false)) { AutoFlush = true })
        {
            try
            {
                Install();
                return 0;
            }
            catch (RestartNeeded)
            {
                Log("Restart Windows to finish enabling WSL, then run Setup again.");
                return 3010;   // the conventional "restart required" installer code
            }
            catch (Exception e)
            {
                Log("Error: " + e.Message);
                return 1;
            }
        }
    }

    void Install()
    {
        // 1. WSL itself
        Status("Checking the Windows Subsystem for Linux...", 2);
        wslInstalled = Wsl.IsInstalled();
        if (!wslInstalled)
        {
            Status("Enabling the Windows Subsystem for Linux (administrator approval needed)...", 4);
            Log("Running: wsl --install --no-distribution");
            CopySelf();
            var psi = new ProcessStartInfo(Wsl.Exe, "--install --no-distribution") { UseShellExecute = true, Verb = "runas" };
            Process p;
            try { p = Process.Start(psi); }
            catch (System.ComponentModel.Win32Exception)
            {
                throw new Exception("Administrator approval is needed to enable the Windows Subsystem for Linux.");
            }
            p.WaitForExit();
            Log("wsl --install finished with exit code " + p.ExitCode);
            if (p.ExitCode != 0)
                throw new Exception("Enabling WSL failed (exit code " + p.ExitCode + ").");
            using (var k = Registry.CurrentUser.CreateSubKey(Product.RunOnceKey))
                k.SetValue("CakeStudioSetup", "\"" + Path.Combine(Product.InstallDir, "Uninstall CakeStudio.exe") + "\" --resume");
            throw new RestartNeeded();
        }

        // 2. Ubuntu 24.04
        Status("Looking for Ubuntu 24.04...", 6);
        state.Distro = Wsl.FindUbuntu2404();
        if (state.Distro == null)
        {
            Status("Installing Ubuntu 24.04. Create your Linux user in the console window, then type exit...", 8);
            Log("Running: wsl --install -d " + Product.UbuntuDistro);
            var psi = new ProcessStartInfo(Wsl.Exe, "--install -d " + Product.UbuntuDistro) { UseShellExecute = true };
            var p = Process.Start(psi);
            p.WaitForExit();
            state.Distro = Wsl.FindUbuntu2404();
            if (state.Distro == null)
                throw new Exception("Ubuntu 24.04 could not be installed (wsl --install exit code " + p.ExitCode + ").");
        }
        Log("Using WSL distribution: " + state.Distro);

        string script = Product.ResourceText("install.sh");
        var errors = new List<string>();

        // 3. System packages, as root so no Linux password is needed
        Status("Installing OpenFOAM and libraries...", 10);
        int rc = Wsl.Run("-d " + state.Distro + " -u root -- bash -s -- --deps", script, ScriptOutput(10, 60, errors));
        if (cancelRequested) throw new OperationCanceledException();
        if (rc != 0) throw new Exception(errors.Count > 0 ? errors[0] : "Installing packages failed (exit code " + rc + ").");

        // 4. CakeStudio itself, as the normal user
        string args = "--app --version " + Product.Version;
        if (!state.Teno) args += " --no-teno";
        string tarball = Environment.GetEnvironmentVariable("CAKESTUDIO_SETUP_TARBALL");
        if (!string.IsNullOrEmpty(tarball)) args += " --tarball \"" + tarball + "\"";
        rc = Wsl.Run("-d " + state.Distro + " -- bash -s -- " + args, script, ScriptOutput(60, 92, errors));
        if (cancelRequested) throw new OperationCanceledException();
        if (rc != 0) throw new Exception(errors.Count > 0 ? errors[0] : "Installing " + Product.Name + " failed (exit code " + rc + ").");

        // 5. Windows launcher, shortcuts and uninstall entry
        Status("Creating shortcuts...", 95);
        InstallWindowsSide();
        SetupState.Clear();
        Status("Completed", 100);
        Log("Completed");
    }

    static void CopySelf()
    {
        Directory.CreateDirectory(Product.InstallDir);
        string self = Assembly.GetExecutingAssembly().Location;
        string dest = Path.Combine(Product.InstallDir, "Uninstall CakeStudio.exe");
        if (!string.Equals(Path.GetFullPath(self), Path.GetFullPath(dest), StringComparison.OrdinalIgnoreCase))
            File.Copy(self, dest, true);
    }

    void InstallWindowsSide()
    {
        string dir = Product.InstallDir;
        Directory.CreateDirectory(dir);
        CopySelf();
        foreach (string res in new[] { "CakeStudio.exe", "CakeStudio.ico", "LICENSE.txt", "TERMS.txt" })
            using (var src = Product.Resource(res))
            using (var dst = File.Create(Path.Combine(dir, res)))
                src.CopyTo(dst);
        // The launcher reads this to start the right distribution
        File.WriteAllText(Path.Combine(dir, "CakeStudio.distro"), state.Distro);
        Log("Installed launcher to " + dir);

        string launcher = Path.Combine(dir, "CakeStudio.exe");
        string icon = Path.Combine(dir, "CakeStudio.ico");
        if (state.Desktop)
            Shortcut(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "CakeStudio.lnk"), launcher, icon);
        if (state.StartMenu)
            Shortcut(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Programs), "CakeStudio.lnk"), launcher, icon);

        using (var k = Registry.CurrentUser.CreateSubKey(Product.UninstallKey))
        {
            string uninst = Path.Combine(dir, "Uninstall CakeStudio.exe");
            k.SetValue("DisplayName", Product.Name);
            k.SetValue("DisplayVersion", Product.Version);
            k.SetValue("Publisher", Product.Publisher);
            k.SetValue("DisplayIcon", icon);
            k.SetValue("InstallLocation", dir);
            k.SetValue("URLInfoAbout", Product.Url);
            k.SetValue("UninstallString", "\"" + uninst + "\" --uninstall");
            k.SetValue("EstimatedSize", 2 * 1024 * 1024, RegistryValueKind.DWord);
            k.SetValue("NoModify", 1, RegistryValueKind.DWord);
            k.SetValue("NoRepair", 1, RegistryValueKind.DWord);
        }
        Log("Registered in Installed apps");
    }

    void Shortcut(string path, string target, string icon)
    {
        Type t = Type.GetTypeFromProgID("WScript.Shell");
        object shell = Activator.CreateInstance(t);
        object lnk = t.InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell, new object[] { path });
        Type lt = lnk.GetType();
        lt.InvokeMember("TargetPath", BindingFlags.SetProperty, null, lnk, new object[] { target });
        lt.InvokeMember("WorkingDirectory", BindingFlags.SetProperty, null, lnk, new object[] { Path.GetDirectoryName(target) });
        lt.InvokeMember("IconLocation", BindingFlags.SetProperty, null, lnk, new object[] { icon + ",0" });
        lt.InvokeMember("Description", BindingFlags.SetProperty, null, lnk, new object[] { "CFD studio for OpenFOAM" });
        lt.InvokeMember("Save", BindingFlags.InvokeMethod, null, lnk, null);
        Log("Created shortcut " + path);
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        bool busy = page == PageId.Installing && !installDone;
        bool midWizard = page < PageId.Installing;
        if ((busy || midWizard) && e.CloseReason == CloseReason.UserClosing)
        {
            string q = busy ? "Are you sure you want to cancel the installation? It can be resumed by running Setup again."
                            : "Are you sure you want to quit " + Product.Name + " Setup?";
            if (MessageBox.Show(this, q, Text, MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes)
            {
                e.Cancel = true;
                return;
            }
            if (busy)
            {
                cancelRequested = true;
                try { if (Wsl.Current != null && !Wsl.Current.HasExited) Wsl.Current.Kill(); } catch (Exception) { }
            }
        }
        base.OnFormClosing(e);
    }

    // Renders every page to PNG files for previews and documentation
    public void SaveScreenshots(string dir)
    {
        Directory.CreateDirectory(dir);
        StartPosition = FormStartPosition.Manual;
        Location = new Point(-4000, -4000);
        ShowInTaskbar = false;
        Show();
        Action<PageId, string> shot = delegate(PageId id, string name)
        {
            page = id;
            foreach (var kv in pages) kv.Value.Visible = kv.Key == id;
            UpdateButtons();
            Refresh();
            Application.DoEvents();
            using (var bmp = new Bitmap(Width, Height))
            {
                DrawToBitmap(bmp, new Rectangle(0, 0, Width, Height));
                bmp.Save(Path.Combine(dir, name + ".png"), System.Drawing.Imaging.ImageFormat.Png);
            }
        };
        shot(PageId.Welcome, "1-welcome");
        acceptRadio.Checked = true;
        shot(PageId.License, "2-license");
        wslInstalled = false; state.Distro = null; ShowLinuxStatus();
        shot(PageId.Linux, "3a-linux-needs-wsl");
        wslInstalled = true; state.Distro = "Ubuntu"; ShowLinuxStatus();
        shot(PageId.Linux, "3b-linux-ready");
        shot(PageId.Components, "4-components");
        FillReady();
        shot(PageId.Ready, "5-ready");
        installStatus.Text = "Installing OpenFOAM v2412 and libraries (this can take a while)";
        progress.Value = 42;
        shot(PageId.Installing, "6a-installing");
        detailsBtn.Visible = false; details.Visible = true;
        foreach (string l in new[] { "Using WSL distribution: Ubuntu", "Preparing the package manager",
                 "Get:1 http://archive.ubuntu.com/ubuntu noble InRelease [256 kB]", "Adding the OpenFOAM package repository",
                 "Installing OpenFOAM v2412 and libraries (this can take a while)", "Unpacking openfoam2412 (2412.260127) ..." })
            details.Items.Add(l);
        shot(PageId.Installing, "6b-installing-details");
        shot(PageId.Restart, "7-restart");
        installDone = true;
        shot(PageId.Finish, "8-finish");
        Close();
    }
}

static class Uninstaller
{
    public static void Run(bool silent)
    {
        if (!silent && MessageBox.Show("Remove " + Product.Name + " from your computer?\r\n\r\nOpenFOAM, Ubuntu and WSL will be kept.",
                            Product.Name + " Uninstall", MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes)
            return;

        string dir = Product.InstallDir;
        string distroFile = Path.Combine(dir, "CakeStudio.distro");
        if (File.Exists(distroFile))
        {
            string distro = File.ReadAllText(distroFile).Trim();
            try { Wsl.Run("-d " + distro + " -- bash -s -- --uninstall", Product.ResourceText("install.sh"), null); }
            catch (Exception) { }
        }
        foreach (string lnk in new[] {
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "CakeStudio.lnk"),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Programs), "CakeStudio.lnk") })
            try { File.Delete(lnk); } catch (Exception) { }
        Registry.CurrentUser.DeleteSubKeyTree(Product.UninstallKey, false);
        SetupState.Clear();

        if (!silent)
            MessageBox.Show(Product.Name + " was removed from your computer.", Product.Name + " Uninstall",
                            MessageBoxButtons.OK, MessageBoxIcon.Information);

        // This exe lives in the install folder, so delete the folder after it exits
        Process.Start(new ProcessStartInfo("cmd.exe",
            "/c timeout /t 2 /nobreak >nul & rmdir /s /q \"" + dir + "\"")
            { CreateNoWindow = true, UseShellExecute = false });
    }
}

static class Program
{
    [STAThread]
    static void Main(string[] args)
    {
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        string mode = args.Length > 0 ? args[0] : "";

        if (mode == "--uninstall") { Uninstaller.Run(args.Length > 1 && args[1] == "--silent"); return; }
        if (mode == "--silent")
        {
            // Passing --silent means accepting the license, as with other installers
            var f = new SetupForm(new SetupState { Accepted = true }, false);
            string log = Path.Combine(Path.GetTempPath(), "CakeStudioSetup.log");
            Environment.ExitCode = f.RunSilent(args.Length > 1 ? args[1] : log);
            return;
        }
        if (mode == "--screenshots")
        {
            var f = new SetupForm(new SetupState(), false);
            f.SaveScreenshots(args.Length > 1 ? args[1] : "screenshots");
            return;
        }
        bool resume = mode == "--resume";
        SetupState state = resume ? SetupState.Load() : new SetupState();
        if (resume && !state.Accepted) resume = false;
        Application.Run(new SetupForm(state, resume));
    }
}
