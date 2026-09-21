# Makes Win/defaultStates.h, the table of each block's default state, from two Minecraft "debug world" saves.
#
# Why: from Minecraft 26.3 on, a block in its default state is saved with no properties at all, so Mineways has to know each block's default
# properties (nbt.cpp, defaultStateProperties). A debug world has every state of every block exactly once, so:
#   - In an older world (e.g., 26.2) every state is saved with all its properties.
#   - In a 26.3+ world, all states are saved with properties except the default one.
#   So the default state of a block is the state that is in the older world but is not listed with properties in the newer world.
# For blocks that are only in the newer world, we can only use the states listed in the newer world, and the default has to be the one
# combination of property values that is missing (which fails when the default has a value that never appears in any other state).
#
# Make sure both debug worlds are fully generated (fly to the far corners) before running this. Usage:
#   powershell -File tools\make_default_states.ps1 -OldWorld "...\saves\26_2 Debug World" -NewWorld "...\saves\26_3 Debug World" -Out Win\defaultStates.h

param(
    [Parameter(Mandatory = $true)][string]$OldWorld,
    [Parameter(Mandatory = $true)][string]$NewWorld,
    [string]$Out = "$PSScriptRoot\..\Win\defaultStates.h"
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
$enc = [Text.Encoding]::GetEncoding(28591)   # bytes as characters, one to one

function ReadStr($s, [ref]$q) {
    $l = ([int][char]$s[$q.Value] * 256) + [int][char]$s[$q.Value + 1]
    $v = $s.Substring($q.Value + 2, $l)
    $q.Value += 2 + $l
    $v
}

# The palette lists, which start with: list tag (9), name length 7, "palette", element type, count.
$pal = [string][char]9 + [string][char]0 + [string][char]7 + 'palette'

# returns block name -> set of state strings ("k=v,k=v" sorted, or "<default>" when no properties are stored)
function Scan($world) {
    $states = @{}
    $regionDir = Join-Path $world 'dimensions\minecraft\overworld\region'
    if (-not (Test-Path $regionDir)) { $regionDir = Join-Path $world 'region' }
    foreach ($f in Get-ChildItem $regionDir -Filter *.mca) {
        $fs = New-Object IO.FileStream($f.FullName, 'Open', 'Read', 'ReadWrite')
        $b = New-Object byte[] $fs.Length
        [void]$fs.Read($b, 0, $b.Length)
        $fs.Close()
        for ($i = 0; $i -lt 1024; $i++) {
            $e = ($b[$i * 4] * 65536) + ($b[$i * 4 + 1] * 256) + $b[$i * 4 + 2]
            if ($e -eq 0) { continue }
            $off = $e * 4096
            $len = ($b[$off] * 16777216) + ($b[$off + 1] * 65536) + ($b[$off + 2] * 256) + $b[$off + 3]
            if ($b[$off + 4] -ne 2) { continue }   # zlib compressed only
            try {
                $ms = New-Object IO.MemoryStream (, $b[($off + 5 + 2)..($off + 4 + $len)])
                $ds = New-Object IO.Compression.DeflateStream($ms, [IO.Compression.CompressionMode]::Decompress)
                $out = New-Object IO.MemoryStream
                $ds.CopyTo($out)
            } catch { continue }
            $s = $enc.GetString($out.ToArray())
            $pos = 0
            while (($pos = $s.IndexOf($pal, $pos)) -ge 0) {
                $p = $pos + $pal.Length
                $et = [int][char]$s[$p]
                $cnt = ([int][char]$s[$p + 1] * 16777216) + ([int][char]$s[$p + 2] * 65536) + ([int][char]$s[$p + 3] * 256) + [int][char]$s[$p + 4]
                $q = [ref]($p + 5)
                $pos = $p
                if ($cnt -gt 4000 -or $cnt -lt 1) { continue }
                if ($et -eq 8) {
                    # list of strings: block names with no properties (or biome names, which we ignore later since they have no properties)
                    for ($k = 0; $k -lt $cnt; $k++) {
                        $n = ReadStr $s $q
                        if (-not $states.ContainsKey($n)) { $states[$n] = New-Object Collections.Generic.HashSet[string] }
                        [void]$states[$n].Add('<default>')
                    }
                } elseif ($et -eq 10) {
                    for ($k = 0; $k -lt $cnt; $k++) {
                        $name = ''
                        $props = @()
                        while ($true) {
                            $t = [int][char]$s[$q.Value]; $q.Value++
                            if ($t -eq 0) { break }
                            $tn = ReadStr $s $q
                            if ($t -eq 8) {
                                $x = ReadStr $s $q
                                if ($tn -in 'Name', 'id', '') { $name = $x }   # "id" and "" (a string in a mixed list) are new in 26.3
                            } elseif ($t -eq 10) {
                                while ($true) {
                                    $t2 = [int][char]$s[$q.Value]; $q.Value++
                                    if ($t2 -eq 0) { break }
                                    $pn = ReadStr $s $q
                                    if ($t2 -eq 8) { $pv = ReadStr $s $q } else { $pv = "?$t2"; break }
                                    $props += "$pn=$pv"
                                }
                            } else { break }
                        }
                        if (-not $states.ContainsKey($name)) { $states[$name] = New-Object Collections.Generic.HashSet[string] }
                        [void]$states[$name].Add($(if ($props.Count) { ($props | Sort-Object) -join ',' } else { '<default>' }))
                    }
                }
            }
        }
    }
    $states
}

$new = Scan $NewWorld
$old = Scan $OldWorld
$rows = New-Object Collections.Generic.List[object]
$notes = New-Object Collections.Generic.List[string]

foreach ($name in ($new.Keys | Sort-Object)) {
    if ($name -notlike 'minecraft:*') { continue }
    $N = $new[$name]
    $comp = @($N | Where-Object { $_ -ne '<default>' })
    $hasDef = $N.Contains('<default>')
    $short = $name.Substring(10)
    if ($old.ContainsKey($name)) {
        $oprops = @($old[$name] | Where-Object { $_ -ne '<default>' })
        if ($oprops.Count -eq 0) { continue }   # no properties, nothing to add
        $missing = @($oprops | Where-Object { -not $N.Contains($_) })
        if ($hasDef -and $missing.Count -eq 1) { $rows.Add([pscustomobject]@{ Name = $short; Props = $missing[0] }) }
        else { $notes.Add("$short : could not tell the default (old states not in new: $($missing.Count), default seen: $hasDef)") }
    } else {
        # only in the new world: use the missing combination of the values seen
        if ($comp.Count -eq 0) { continue }
        $vals = @{}
        foreach ($c in $comp) { foreach ($kv in $c.Split(',')) { $kk = $kv.Split('='); if (-not $vals.ContainsKey($kk[0])) { $vals[$kk[0]] = New-Object Collections.Generic.HashSet[string] }; [void]$vals[$kk[0]].Add($kk[1]) } }
        $keys = @($vals.Keys | Sort-Object)
        $combos = New-Object Collections.Generic.List[string]; $combos.Add('')
        foreach ($k in $keys) { $next = New-Object Collections.Generic.List[string]; foreach ($cb in $combos) { foreach ($v in ($vals[$k] | Sort-Object)) { $next.Add($(if ($cb) { "$cb,$k=$v" } else { "$k=$v" })) } }; $combos = $next }
        $have = New-Object Collections.Generic.HashSet[string]; foreach ($c in $comp) { [void]$have.Add($c) }
        $missing = @($combos | Where-Object { -not $have.Contains($_) })
        if ($hasDef -and $missing.Count -eq 1) { $rows.Add([pscustomobject]@{ Name = $short; Props = $missing[0] }); $notes.Add("$short : new block, default from the missing combination") }
        else { $notes.Add("$short : new block, could not tell the default") }
    }
}

# strcmp order is needed for the binary search in nbt.cpp, so sort by ordinal
$list = New-Object Collections.Generic.List[object]; $list.AddRange($rows)
$list.Sort([Comparison[object]] { param($a, $b) [string]::CompareOrdinal($a.Name, $b.Name) })

$sb = New-Object Text.StringBuilder
[void]$sb.AppendLine('// GENERATED by tools/make_default_states.ps1 from Minecraft debug worlds - do not edit by hand; see that script.')
[void]$sb.AppendLine('// The default state of each block that has properties, as "name=value,name=value". Minecraft 26.3 and later do not save these properties for a block in its default state.')
[void]$sb.AppendLine('// Sorted by name (strcmp order) for a binary search. Names are without the "minecraft:".')
[void]$sb.AppendLine('static const struct { const char* name; const char* props; } gDefaultStates[] = {')
foreach ($r in $list) { [void]$sb.AppendLine('    { "' + $r.Name + '", "' + $r.Props + '" },') }
[void]$sb.AppendLine('};')
[void]$sb.AppendLine('#define NUM_DEFAULT_STATES ((int)(sizeof(gDefaultStates) / sizeof(gDefaultStates[0])))')
$text = $sb.ToString() -replace "`r?`n", "`r`n"
[IO.File]::WriteAllText((Resolve-Path (Split-Path $Out -Parent)).Path + '\' + (Split-Path $Out -Leaf), $text, (New-Object Text.UTF8Encoding($false)))
"wrote $($list.Count) default states to $Out"
$notes | Sort-Object | ForEach-Object { "  note: $_" }
