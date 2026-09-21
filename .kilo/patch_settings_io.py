from pathlib import Path

path = Path(r'E:\PWA\cpr-vcodex-steroids\src\JsonSettingsIOShared.inc')
lines = path.read_text(encoding='utf-8').splitlines()

# Find the start of saveJsonDocumentToFile and the start of loadJsonDocumentFromFile
start_idx = None
end_idx = None
for i, line in enumerate(lines):
    if line.strip().startswith('static bool saveJsonDocumentToFile('):
        start_idx = i
    if start_idx is not None and line.strip().startswith('static bool loadJsonDocumentFromFile('):
        end_idx = i
        break

if start_idx is None or end_idx is None:
    raise SystemExit(f'Could not locate function bounds: start={start_idx}, end={end_idx}')

insert_before = [
    '',
    'static bool promoteJsonTempFile(const char* moduleName, const char* tempPath, const char* targetPath,',
    '                         const size_t expectedSize) {',
    '  if (Storage.exists(targetPath) && !Storage.remove(targetPath)) {',
    '    Storage.remove(tempPath);',
    '    LOG_ERR(moduleName, "Could not remove JSON file before replace: %s", targetPath);',
    '    CPR_VCODEX_LOG_EVENT(moduleName, std::string("Could not remove JSON file before replace: ") + targetPath);',
    '    return false;',
    '  }',
    '',
    '  if (!Storage.rename(tempPath, targetPath)) {',
    '    LOG_ERR(moduleName, "Could not rename JSON temp file to final path: %s; trying checked copy", targetPath);',
    '    CPR_VCODEX_LOG_EVENT(moduleName, std::string("JSON temp rename failed; trying checked copy for ") + targetPath);',
    '',
    '    if (!copyVerifiedJsonTempToTarget(moduleName, tempPath, targetPath, expectedSize)) {',
    '      LOG_ERR(moduleName, "Could not promote JSON temp file to final path: %s", targetPath);',
    '      CPR_VCODEX_LOG_EVENT(moduleName,',
    '                           std::string("Could not promote JSON temp file; kept it for recovery: ") + tempPath);',
    '      return false;',
    '    }',
    '',
    '    Storage.remove(tempPath);',
    '    LOG_DBG(moduleName, "Recovered JSON replacement via checked copy: %s", targetPath);',
    '    CPR_VCODEX_LOG_EVENT(moduleName, std::string("Recovered JSON replacement via checked copy: ") + targetPath);',
    '  }',
    '',
    '  return true;',
    '}',
]

# Replace the final promotion block inside saveJsonDocumentToFile with a call to promoteJsonTempFile.
for i in range(start_idx, end_idx):
    stripped = lines[i].strip()
    if stripped == 'if (Storage.exists(targetPath.c_str()) && !Storage.remove(targetPath.c_str())) {' and \
       lines[i+1].strip() == 'Storage.remove(tempPath.c_str());' and \
       lines[i+2].strip() == 'LOG_ERR(moduleName, "Could not remove JSON file before replace: %s", targetPath.c_str());' and \
       lines[i+3].strip() == 'CPR_VCODEX_LOG_EVENT(moduleName, std::string("Could not remove JSON file before replace: ") + targetPath);' and \
       lines[i+4].strip() == 'return false;' and \
       lines[i+5].strip() == '}' and \
       lines[i+6].strip() == '' and \
       lines[i+7].strip() == 'if (!Storage.rename(tempPath.c_str(), targetPath.c_str())) {' and \
       lines[i+8].strip() == 'LOG_ERR(moduleName, "Could not rename JSON temp file to final path: %s; trying checked copy", targetPath.c_str());' and \
       lines[i+9].strip() == 'CPR_VCODEX_LOG_EVENT(moduleName,' and \
       lines[i+10].strip() == 'std::string("JSON temp rename failed; trying checked copy for ") + targetPath);' and \
       lines[i+11].strip() == '' and \
       lines[i+12].strip() == 'if (!copyVerifiedJsonTempToTarget(moduleName, tempPath, targetPath, expected)) {' and \
       lines[i+13].strip() == 'LOG_ERR(moduleName, "Could not promote JSON temp file to final path: %s", targetPath.c_str());' and \
       lines[i+14].strip() == 'CPR_VCODEX_LOG_EVENT(moduleName,' and \
       lines[i+15].strip() == 'std::string("Could not promote JSON temp file; kept it for recovery: ") + tempPath);' and \
       lines[i+16].strip() == 'return false;' and \
       lines[i+17].strip() == '}' and \
       lines[i+18].strip() == '' and \
       lines[i+19].strip() == 'Storage.remove(tempPath.c_str());' and \
       lines[i+20].strip() == 'LOG_DBG(moduleName, "Recovered JSON replacement via checked copy: %s", targetPath.c_str());' and \
       lines[i+21].strip() == 'CPR_VCODEX_LOG_EVENT(moduleName,' and \
       lines[i+22].strip() == 'std::string("Recovered JSON replacement via checked copy: ") + targetPath);' and \
       lines[i+23].strip() == '  }' and \
       lines[i+24].strip() == '' and \
       lines[i+25].strip() == '  return true;' and \
       lines[i+26].strip() == '}':
        # Remove from line i to i+26 inclusive and replace with a single return
        lines[i:i+27] = ['', '  return promoteJsonTempFile(moduleName, tempPath.c_str(), targetPath.c_str(), written);']
        break
else:
    raise SystemExit('Could not locate promotion block inside saveJsonDocumentToFile')

# Insert promoteJsonTempFile before saveJsonDocumentToFile
lines.insert(start_idx, '\n'.join(insert_before) + '\n')

path.write_text('\n'.join(lines), encoding='utf-8')
print('patched')
