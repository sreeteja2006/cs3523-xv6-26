import subprocess
import time

print("Starting QEMU...")
p = subprocess.Popen(['make', 'qemu'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

def read_until_prompt():
    out = b""
    while p.poll() is None:
        try:
            b = p.stdout.read(1)
            if not b:
                break
            out += b
            if out.endswith(b"$ "):
                break
        except Exception:
            break
    print(out.decode('utf-8', errors='ignore'))
    return out

tests = ['H', 'I', 'J', 'K', 'L', 'M', 'N']

read_until_prompt()
for t in tests:
    print(f"=== RUNNING test {t} ===")
    p.stdin.write((t + '\n').encode())
    p.stdin.flush()
    time.sleep(0.5)
    read_until_prompt()

print("=== EXITING ===")
p.stdin.write(b'\x01x\n')
p.stdin.flush()
time.sleep(1)
p.terminate()
