param([Parameter(Mandatory=$true)][int]$MainProcessId,[int]$Samples=40)
$ErrorActionPreference='Stop'
$peakWorking=0.0; $peakPrivate=0.0; $peakMembers=@(); $last=@()
for($sampleIndex=0;$sampleIndex -lt $Samples;$sampleIndex++) {
    $rows=@(Get-CimInstance Win32_Process | Select-Object ProcessId,ParentProcessId,CreationDate,Name)
    $rootRow=$rows | Where-Object ProcessId -eq $MainProcessId | Select-Object -First 1
    if(!$rootRow){throw 'The monitored app has exited.'}
    $births=[System.Collections.Generic.Dictionary[int,datetime]]::new()
    $births.Add($MainProcessId,$rootRow.CreationDate)
    do {
        $added=$false
        foreach($entry in $rows) {
            $parentId=[int]$entry.ParentProcessId; $childId=[int]$entry.ProcessId
            # ParentProcessId can refer to a recycled PID. Exclude older orphaned processes.
            if(!$births.ContainsKey($childId) -and $births.ContainsKey($parentId) -and $entry.CreationDate -ge $births[$parentId]) {
                $births.Add($childId,$entry.CreationDate);$added=$true
            }
        }
    } while($added)
    $members=@(foreach($entry in $rows) {
        if($births.ContainsKey([int]$entry.ProcessId)) {
            $process=Get-Process -Id $entry.ProcessId -ErrorAction SilentlyContinue
            if($process) {
                [pscustomobject]@{name=$entry.Name;id=$entry.ProcessId;parent=$entry.ParentProcessId;
                    workingMiB=[math]::Round($process.WorkingSet64/1MB,2);privateMiB=[math]::Round($process.PrivateMemorySize64/1MB,2)}
            }
        }
    })
    $working=($members | Measure-Object workingMiB -Sum).Sum
    $private=($members | Measure-Object privateMiB -Sum).Sum
    if($working -gt $peakWorking){$peakWorking=$working;$peakMembers=$members}
    $peakPrivate=[math]::Max($private,$peakPrivate);$last=$members
    Start-Sleep -Milliseconds 250
}
[pscustomobject]@{samples=$Samples;peakTreeWorkingMiB=[math]::Round($peakWorking,2);
    peakTreePrivateMiB=[math]::Round($peakPrivate,2);peakMembers=$peakMembers;lastMembers=$last}
