import os
import re
import glob

icons_dir = r"E:\PWA\cpr-vcodex-steroids\src\components\icons"
icon_files = glob.glob(os.path.join(icons_dir, "*.h"))

pgmspace_line = '#include <pgmspace.h>\n'
# Match "static const uint8_t Name[] = {" capturing "Name[] = {"
pattern_re = re.compile(r'^(static const uint8_t\s+)(\w+\[\]\s*=\s*\{)$', re.MULTILINE)

modified = 0
already_had = 0
errors = []

for fpath in sorted(icon_files):
    fname = os.path.basename(fpath)
    with open(fpath, 'r', encoding='utf-8', errors='replace') as f:
        content = f.read()
    original = content

    # 1. Add pgmspace.h after #include <cstdint> if not already present
    has_pgmspace = '#include <pgmspace.h>' in content
    if not has_pgmspace:
        content = content.replace('#include <cstdint>', '#include <cstdint>\n#include <pgmspace.h>', 1)
    
    # 2. Replace "static const uint8_t Name[] = {" with "static const uint8_t PROGMEM Name[] = {"
    content = pattern_re.sub(r'\1PROGMEM \2', content)
    
    # 3. Verify PROGMEM was inserted
    if 'PROGMEM' not in content:
        errors.append(f"{fname}: PROGMEM not added!")
        continue
    
    if content != original:
        with open(fpath, 'w', encoding='utf-8') as f:
            f.write(content)
        modified += 1
        print(f"  MODIFIED: {fname}")
    else:
        already_had += 1

print(f"\nDone: {modified} modified, {already_had} already had PROGMEM")
if errors:
    print(f"Errors: {len(errors)}")
    for e in errors:
        print(f"  {e}")
