$d = Get-ChildItem "C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\bin\win64\*.mdmp" | Sort-Object LastWriteTime -Descending | Select-Object -First 3
$d | Format-Table Name,LastWriteTime,Length
