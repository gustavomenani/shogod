$p='C:\Users\User\AppData\Roaming\shogod\logs\shogod-20261119.log'
$fs=[System.IO.File]::Open($p,'Open','Read','ReadWrite')
$sr=New-Object System.IO.StreamReader($fs)
$text=$sr.ReadToEnd()
$sr.Close(); $fs.Close()
$text -split "`n" | Select-String -Pattern 'create_move|CreateMove|hooks:' | Select-Object -Last 8
