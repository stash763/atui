<#
    console_buffer.ps1 - read what a running console program actually wrote.

        powershell -NoProfile -File console_buffer.ps1 -ProcId 4711 -Rows
        powershell -NoProfile -File console_buffer.ps1 -ProcId 4711 -CodePoints
        powershell -NoProfile -File console_buffer.ps1 -ProcId 4711 -Out C:\tmp\screen.txt

    A screenshot answers "what did the font draw", which is a question about the font.
    This answers the other question, the one about atui: it attaches to the target's
    console and reads the screen buffer as the UTF-16 code units the program put there,
    via ReadConsoleOutputCharacterW.  The two can disagree -- a code point the font has
    no glyph for is present in the buffer and shows on screen as a box -- and P4 needs
    to tell those apart, so both are measured.

    The output of this script is itself text crossing a boundary, so two of its options
    exist to stop it lying.  A console's output code page will happily replace a code
    point it cannot encode: on the measured host, code page 437 turned an em dash into a
    hyphen and an e-acute into a question mark, which reads as if the buffer held those
    ASCII characters.  -CodePoints prints every non-ASCII character as U+XXXX, so an
    unwritable character is impossible to mistake for a written one.  -Out writes the
    file as UTF-8 straight from the buffer, past every code page.  Neither is paranoia:
    the first version of this script reported em dash as "-" and did not notice.

    Requires the process id of a program that owns a console.  Attaching means this
    process gives up the console it has, so run it as a one-shot, not dot-sourced into
    a shell you care about.  Rows limits output to the visible window rather than the
    whole scrollback; Attribs prefixes each row with the per-cell color attributes in
    hex, which is how you check a dimmed line is dimmed rather than merely quiet.

    One thing it cannot read, and the reason is not a bug in this script: a program that
    called SetConsoleActiveScreenBuffer to draw on a buffer it created has a console that
    refuses every outside reader (AttachConsole fails with error 32).  Measured across
    eight console operations in tools/win/attach_probe.c; only that operation does it.
    atui is such a program, so this script is for the programs around atui -- cmd,
    PowerShell, a compiler -- and what atui itself drew has to come from atui.

    Windows PowerShell 5.1 has no [short] accelerator; [int16] is spelled out because of
    that, and the failure to avoid it is a silent one -- the assignment throws, the row
    coordinate stays 0, and every row of the dump repeats the first.
#>
param(
    [Parameter(Mandatory = $true)][int]$ProcId,
    [switch]$Rows,
    [switch]$Attribs,
    [switch]$CodePoints,
    [string]$Out
)

if ($PSVersionTable.PSVersion.Major -lt 5) { Write-Error "needs PowerShell 5 or newer"; exit 2 }

Add-Type -Namespace Win32 -Name Buf -MemberDefinition @'
    [StructLayout(LayoutKind.Sequential)]
    public struct COORD { public short X; public short Y; }

    [StructLayout(LayoutKind.Sequential)]
    public struct SMALL_RECT { public short Left; public short Top; public short Right; public short Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    public struct CSBI {
        public COORD dwSize;
        public COORD dwCursorPosition;
        public ushort wAttributes;
        public SMALL_RECT window;
        public COORD dwMaximumWindowSize;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool FreeConsole();

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool AttachConsole(int pid);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr CreateFile(string name, int access, int share, IntPtr sa, int mode, int flags, IntPtr template);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool GetConsoleScreenBufferInfo(IntPtr h, out CSBI info);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern bool ReadConsoleOutputCharacterW(IntPtr h, [Out] char[] buf, int len, COORD from, out int read);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool ReadConsoleOutputAttribute(IntPtr h, [Out] ushort[] attrs, int len, COORD from, out int read);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern int GetConsoleOutputCP();
'@

$problem = $null
if (-not [Win32.Buf]::AttachConsole($ProcId)) {
    # A process that already has a console cannot attach; drop ours and try again.
    [void][Win32.Buf]::FreeConsole()
    if (-not [Win32.Buf]::AttachConsole($ProcId)) {
        $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
        $problem = "cannot attach to the console of pid {0} (win32 {1})" -f $ProcId, $err
        if ($err -eq 32) {
            # ERROR_SHARING_VIOLATION.  Measured, not assumed: this is what every program
            # that called SetConsoleActiveScreenBuffer does to its own console, whatever
            # share mode it asked for, so no change of this script's or of the target's
            # share mode will help.  See tools/win/attach_probe.c for the eight-case
            # measurement.  atui is such a program, so to read what atui drew, ask atui.
            $problem += ": error 32 means the target swapped in a screen buffer of its own," +
                        " and conhost then refuses every reader of that screen"
        } else {
            $problem += ": is that pid running, and does it own a console?"
        }
    }
}
if ($problem) {
    # Written to the success stream, deliberately.  FreeConsole above may have left this
    # process with no console, and PowerShell's warning and error writers go looking for
    # one: the first version reported 'The Win32 internal error "The handle is invalid"
    # 0x6' instead of the reason, because the message about the failure needed a console
    # the script had just given up.  stdout is whatever the caller handed us and survives.
    $problem
    exit 1
}

# CONOUT$ is the screen buffer of the console this process is now attached to.
# GENERIC_READ only: this script observes, it does not draw.  A program that wants its
# screen to stay unreadable is not stopped from this anyway -- we never reach here if the
# console refuses the attach, and that is the case that matters (see attach_probe.c).
$h = [Win32.Buf]::CreateFile('\\.\CONOUT$', 0x80000000, 0x3, [IntPtr]::Zero, 3, 0, [IntPtr]::Zero)
if ($h.ToInt64() -eq -1) {
    "cannot open CONOUT$ of pid {0} (win32 {1})" -f $ProcId,
        [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
    exit 1
}

$info = New-Object Win32.Buf+CSBI
if (-not [Win32.Buf]::GetConsoleScreenBufferInfo($h, [ref]$info)) {
    $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
    "GetConsoleScreenBufferInfo failed (win32 $err)"
    exit 1
}

$cols = [int]$info.dwSize.X
$first = 0
$last = [int]$info.dwSize.Y - 1
if ($Rows) { $first = [int]$info.window.Top; $last = [int]$info.window.Bottom }

$header = "buffer {0}x{1}  window {2}x{3} at ({4},{5})  cursor ({6},{7})  output CP {8}" -f `
    $cols, $info.dwSize.Y,
    ([int]$info.window.Right - [int]$info.window.Left + 1), ([int]$info.window.Bottom - [int]$info.window.Top + 1),
    $info.window.Left, $info.window.Top,
    $info.dwCursorPosition.X, $info.dwCursorPosition.Y,
    [Win32.Buf]::GetConsoleOutputCP()

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add($header)

for ($y = $first; $y -le $last; $y++) {
    $chars = New-Object char[] $cols
    $n = 0
    $from = New-Object Win32.Buf+COORD
    $from.X = [int16]0
    $from.Y = [int16]$y
    if (-not [Win32.Buf]::ReadConsoleOutputCharacterW($h, $chars, $cols, $from, [ref]$n)) {
        $lines.Add(("{0,3}| READ FAILED at row {1}" -f $y, $y))
        break
    }
    $line = -join $chars
    if ($CodePoints) {
        # every non-ASCII character spelled out, so no code page downstream can
        # replace it with something that looks intentional
        $sb = New-Object System.Text.StringBuilder
        foreach ($ch in $chars) {
            if ([int]$ch -lt 32 -or [int]$ch -eq 32) { [void]$sb.Append(' ') }
            elseif ([int]$ch -lt 127) { [void]$sb.Append($ch) }
            else { [void]$sb.Append(("<U+{0:X4}>" -f [int]$ch)) }
        }
        $line = $sb.ToString()
    }
    $attr = ''
    if ($Attribs) {
        $a = New-Object ushort[] $cols
        [void][Win32.Buf]::ReadConsoleOutputAttribute($h, $a, $cols, $from, [ref]$n)
        $attr = '|' + (($a | ForEach-Object { $_.ToString('x2') }) -join '')
    }
    # trailing blanks are noise in text but content with attributes: a painted space
    # is a color change, and the hex pairs have to line up with the cells
    if ($line.TrimEnd().Length -eq 0 -and -not $Attribs) { continue }
    if (-not $Attribs) { $line = $line.TrimEnd() }
    $lines.Add(("{0,3}|{1}{2}" -f $y, $attr, $line))
}

if ($Out) {
    # straight to bytes: no console, no code page, no question mark
    [System.IO.File]::WriteAllLines($Out, $lines, (New-Object System.Text.UTF8Encoding($false)))
    "wrote {0} lines to {1}" -f $lines.Count, $Out
    $header
} else {
    # Deliberately no [Console]::OutputEncoding here.  Setting it on an attached console
    # changes the code page of the console belonging to the program being measured: the
    # first version of this script flipped the target from 437 to 65001 and then reported
    # 65001 as if that had been the finding.  Non-ASCII text leaving this process crosses
    # that console's code page and may arrive replaced, so ask for -CodePoints (ASCII
    # output, nothing to replace) or -Out (bytes, nothing to replace) when the characters
    # are what you are measuring.  The caveat is printed as part of the output rather than
    # as a warning: after FreeConsole there may be no console for a warning to reach.
    if (-not $CodePoints -and -not $Out) {
        "note: text below crosses this console's code page; -CodePoints or -Out to measure characters"
    }
    $lines -join [Environment]::NewLine
}
