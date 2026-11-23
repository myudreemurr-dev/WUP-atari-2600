#!/usr/bin/env python3
"""Send files to the GamePad's File Sharing (FTP) one at a time, patiently.

The pad's FTP server is tiny: it dislikes parallel connections, directory listings and idle time. This script opens ONE
connection, never lists anything, sends one file, and starts again with a fresh connection if it fails.

    python3 send_to_pad.py PAD_IP FILE REMOTE_PATH [FILE REMOTE_PATH ...]
    python3 send_to_pad.py 192.168.1.49 player.wup apps/atari/player.wup
    python3 send_to_pad.py 192.168.1.49 yars.bin apps/atari/roms/Yars_Revenge.bin
"""
import ftplib, os, sys, time

TRIES = 8


def send(ip, local, remote):
    size = os.path.getsize(local)
    folder, name = os.path.split(remote.strip('/'))
    for attempt in range(1, TRIES + 1):
        ftp = ftplib.FTP()
        try:
            ftp.connect(ip, 21, timeout=25)
            ftp.login('anonymous', '')
            ftp.set_pasv(True)
            for part in [p for p in folder.split('/') if p]:       # apps / atari / roms
                try:
                    ftp.cwd(part)
                except ftplib.error_perm:
                    ftp.mkd(part); ftp.cwd(part)
            sent = [0]

            def progress(block):
                sent[0] += len(block)
                if sent[0] % 16384 < len(block) or sent[0] >= size:
                    print('\r  %s: %d / %d KB' % (name, sent[0] // 1024, size // 1024), end='', flush=True)
            with open(local, 'rb') as f:
                ftp.storbinary('STOR ' + name, f, 1024, progress)
            print('\r  %s: sent (%d bytes)            ' % (name, size))
            try:
                ftp.quit()
            except Exception:
                pass
            return True
        except Exception as e:
            print('\n  attempt %d/%d failed: %s' % (attempt, TRIES, e))
            try:
                ftp.close()
            except Exception:
                pass
            time.sleep(4)                                              # let the pad end the broken session
    return False


def main():
    a = sys.argv[1:]
    if len(a) < 3 or len(a) % 2 == 0:
        sys.exit(__doc__)
    ip = a[0]; ok = True
    for i in range(1, len(a), 2):
        print('->', a[i], 'to', a[i + 1])
        ok = send(ip, a[i], a[i + 1]) and ok
    print('done' if ok else 'SOME FILES FAILED: toggle File Sharing on the pad (off, on) and run it again')
    sys.exit(0 if ok else 1)


main()
