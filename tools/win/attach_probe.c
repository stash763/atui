/*
 * attach_probe.c -- which console operation makes AttachConsole() refuse a process?
 *
 *     cl /Fe attach_probe.exe attach_probe.c
 *     start attach_probe.exe b          (it prints a line and waits)
 *     powershell -File tools/win/console_buffer.ps1 -ProcId <pid>   (from another machine
 *                                                                     or window, any reader)
 *
 * Background. tools/win/console_buffer.ps1 reads the screen of a running console program
 * by calling AttachConsole() and then reading CONOUT$. Against ordinary programs, and
 * against PowerShell and cmd, that works. Against atui it fails with ERROR_SHARING_VIOLATION
 * (32). Something atui does to its console at startup is the cause; this file exists to
 * find out which of those operations it is, one at a time, instead of blaming the first
 * plausible-looking one.
 *
 * The modes are the console-level operations tvision's ConsoleCtl performs, plus the two
 * ways a program can relate to a console at all:
 *
 *   a  nothing                     a baseline: a waiting program in an ordinary console
 *   b  own buffer, share mode 0    + SetConsoleActiveScreenBuffer
 *   c  own buffer, FILE_SHARE_READ + SetConsoleActiveScreenBuffer
 *   d  as b, and SetConsoleScreenBufferSize first
 *   e  SetConsoleMode(input, 0)    raw input, no echo, no window events
 *   f  SetConsoleWindowInfo        shrink the window
 *   g  FreeConsole()               keep running with no console attached
 *   h  FreeConsole(); AllocConsole run in a console the process itself owns
 *
 * A mode "explains" the atui failure when a reader of this program's screen fails with
 * error 32 in exactly that mode. Measured on Windows 10 Pro 19045 with conhost, 24 Nov 2026:
 *
 *   a nothing                          readable
 *   b own buffer, share mode 0         ERROR_SHARING_VIOLATION
 *   c own buffer, FILE_SHARE_READ      ERROR_SHARING_VIOLATION
 *   d as b, buffer size set first      ERROR_SHARING_VIOLATION
 *   e raw input mode                   readable
 *   f window shrunk                    readable
 *   g console detached, still running  readable
 *   h own console (AllocConsole)       readable
 *
 * So the operation that shuts readers out is SetConsoleActiveScreenBuffer and nothing
 * else. That b and c behave identically shows the buffer's share mode is not the reason --
 * worth writing down, because the first attempt at this problem changed that share mode,
 * on a plausible story, and measured no difference. A program that draws on a screen buffer
 * it created, whatever its share mode, has a screen no other process can open: not just a
 * tool like this file's companion, a screen reader too. atui does that in tvision's
 * ConsoleCtl, so reading atui's screen from outside is not available, and measuring what
 * atui drew means asking atui itself -- see WINDOWS_PORT.md.
 */
#include <windows.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    char mode = (argc > 1) ? argv[1][0] : 'a';
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    HANDLE buffer = INVALID_HANDLE_VALUE;

    if (GetConsoleScreenBufferInfo(out, &info))
        printf("mode %c: startup buffer %dx%d, window %dx%d\n", mode,
               info.dwSize.X, info.dwSize.Y,
               info.srWindow.Right - info.srWindow.Left + 1,
               info.srWindow.Bottom - info.srWindow.Top + 1);
    else
        printf("mode %c: no console screen buffer at startup (win32 %lu)\n",
               mode, GetLastError());

    switch (mode) {
    case 'b':
    case 'c':
        buffer = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE,
                                           mode == 'b' ? 0 : FILE_SHARE_READ,
                                           NULL, CONSOLE_TEXTMODE_BUFFER, NULL);
        SetConsoleActiveScreenBuffer(buffer);
        break;
    case 'd':
        buffer = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE, 0, NULL,
                                           CONSOLE_TEXTMODE_BUFFER, NULL);
        if (GetConsoleScreenBufferInfo(out, &info))
            SetConsoleScreenBufferSize(buffer, info.dwSize);
        SetConsoleActiveScreenBuffer(buffer);
        break;
    case 'e':
        SetConsoleMode(in, 0);
        break;
    case 'f':
        if (GetConsoleScreenBufferInfo(out, &info)) {
            info.srWindow.Right -= 4;
            SetConsoleWindowInfo(out, TRUE, &info.srWindow);
        }
        break;
    case 'h':
        FreeConsole();
        AllocConsole();
        printf("mode h: allocated my own console\n");
        break;
    default:
        break;
    }

    printf("mode %c: waiting, read this screen from somewhere else\n", mode);
    fflush(stdout);
    Sleep(180000);

    if (mode == 'g')
        FreeConsole();
    return 0;
}
