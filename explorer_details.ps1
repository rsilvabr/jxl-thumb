# Photo details (camera, lens, ISO, date taken, title) for .jxl files in
# Windows Explorer. Called by enable_explorer_details.cmd and
# disable_explorer_details.cmd; "status" only reports, "hint" prints one line
# for install.cmd when the details are missing and could be turned on.
#
# Windows reads a photo's details through a "property handler". For .jpg,
# .png, .tif, .heic, .webp, .avif and camera raws it registers its own,
# {a38b883c-...} ("IPropertyStore Handler for Images"), which reads EXIF and
# XMP through the image's WIC decoder. Windows 11's JPEG XL decoder hands that
# metadata over, but .jxl may have no property handler at all, so Explorer
# shows only the size and dates. This registers the same Windows handler for
# .jxl - it adds no code of jxl-thumb's to Explorer.
#
# The handler key lives under HKLM (Windows ignores a per-user one), so that
# one step asks for administrator rights; the list of details Explorer shows
# is written per user (HKCU). Nothing is changed when another program already
# registered a property handler for .jxl, or when Windows has no JPEG XL
# decoder. disable undoes only what enable did (remembered under
# HKCU\Software\jxl-thumb).

param([ValidateSet('enable', 'disable', 'status', 'hint')][string]$Action = 'status')

$ErrorActionPreference = 'Stop'
$Handler = '{a38b883c-1682-497e-97b0-0a3a9e801682}'
$HandlerKey = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\PropertySystem\PropertyHandlers\.jxl'
$AssocKey = 'HKCU:\Software\Classes\SystemFileAssociations\.jxl'
$JpgAssoc = 'Registry::HKEY_CLASSES_ROOT\SystemFileAssociations\.jpg'
$MarkKey = 'HKCU:\Software\jxl-thumb\ExplorerDetails'
$DetailValues = 'FullDetails', 'PreviewDetails', 'InfoTip'

function Get-CurrentHandler {
    if (Test-Path $HandlerKey) { return [string](Get-ItemProperty $HandlerKey).'(default)' }
    return ''
}

function Get-JxlDecoder {
    # WIC decoders: HKCR\CLSID\{7ED96837-...}\Instance\{clsid}; the decoder's own
    # key lists the extensions it opens.
    $instances = 'Registry::HKEY_CLASSES_ROOT\CLSID\{7ED96837-96F0-4812-B211-F13C24117ED3}\Instance'
    foreach ($i in Get-ChildItem $instances -ErrorAction SilentlyContinue) {
        $c = Get-ItemProperty "Registry::HKEY_CLASSES_ROOT\CLSID\$($i.PSChildName)" -ErrorAction SilentlyContinue
        if ($c -and ($c.FileExtensions -split ',') -contains '.jxl') { return [string]$c.FriendlyName }
    }
    return ''
}

function Invoke-Elevated([string]$script) {
    # Only the HKLM step runs elevated: HKCU stays the user's own even when the
    # administrator account is another one.
    $enc = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($script))
    try {
        $p = Start-Process powershell -Verb RunAs -Wait -PassThru `
            -ArgumentList "-NoProfile -ExecutionPolicy Bypass -EncodedCommand $enc"
        return ($p.ExitCode -eq 0)
    } catch {
        return $false        # the administrator prompt was declined
    }
}

function Show-Status {
    $h = Get-CurrentHandler
    $d = Get-JxlDecoder
    "JPEG XL decoder (WIC):   " + $(if ($d) { $d } else { 'none' })
    "Property handler (.jxl): " + $(if (-not $h) { 'none' } elseif ($h -ieq $Handler) { "Windows' image handler (details on)" } else { "another program's: $h" })
    $a = Get-ItemProperty 'Registry::HKEY_CLASSES_ROOT\SystemFileAssociations\.jxl' -ErrorAction SilentlyContinue
    "Details list:            " + $(if ($a -and $a.FullDetails -match 'System\.Photo\.CameraModel') { 'photo details' } else { 'size and dates only' })
}

switch ($Action) {
    'status' { Show-Status; exit 0 }

    'hint' {
        if ((Get-JxlDecoder) -and -not (Get-CurrentHandler)) {
            'Explorer shows no photo details (camera, ISO, title) for .jxl files on this PC.'
            'Run enable_explorer_details.cmd to turn them on (asks for administrator rights once).'
        }
        exit 0
    }

    'enable' {
        if (-not (Get-JxlDecoder)) {
            'Windows has no JPEG XL decoder (WIC), so it cannot read the details of a .jxl file.'
            'Install the JPEG XL Image Extension from the Microsoft Store, then run this again.'
            'Nothing was changed.'
            exit 1
        }
        New-Item $MarkKey -Force | Out-Null
        $mark = Get-ItemProperty $MarkKey

        # 1. The list of details, per user. The values that were there before are kept
        #    so disable can put them back.
        if (-not $mark.DetailsSet) {
            $before = Get-ItemProperty $AssocKey -ErrorAction SilentlyContinue
            $jpg = Get-ItemProperty $JpgAssoc
            New-Item $AssocKey -Force | Out-Null
            foreach ($n in $DetailValues) {
                if ($before -and $null -ne $before.$n) { Set-ItemProperty $MarkKey -Name "Prev_$n" -Value $before.$n }
                Set-ItemProperty $AssocKey -Name $n -Value $jpg.$n
            }
            Set-ItemProperty $MarkKey -Name DetailsSet -Value 1 -Type DWord
        }

        # 2. The property handler (HKLM, administrator).
        $h = Get-CurrentHandler
        if ($h -ieq $Handler) {
            "Windows' image property handler is already registered for .jxl."
        } elseif ($h) {
            "Another program registered a property handler for .jxl ($h); it was left alone."
        } else {
            'Registering it for .jxl needs administrator rights: Windows asks now.'
            $ok = Invoke-Elevated ("New-Item '$HandlerKey' -Force | Set-ItemProperty -Name '(default)' -Value '$Handler'")
            if (-not $ok -or (Get-CurrentHandler) -ine $Handler) {
                'The property handler was NOT registered (the administrator prompt was declined or failed).'
                'The details list was set; run this again to finish.'
                exit 1
            }
            Set-ItemProperty $MarkKey -Name HandlerSet -Value 1 -Type DWord
        }
        ''
        'Explorer now shows the photo details of .jxl files (open a new Explorer window).'
        exit 0
    }

    'disable' {
        $mark = Get-ItemProperty $MarkKey -ErrorAction SilentlyContinue
        if (-not $mark) { 'enable_explorer_details.cmd was not run for this user: nothing to undo.'; exit 0 }
        if ($mark.HandlerSet) {
            if ((Get-CurrentHandler) -ieq $Handler) {
                'Removing the property handler needs administrator rights: Windows asks now.'
                if (-not (Invoke-Elevated "Remove-Item '$HandlerKey' -Force") -or (Get-CurrentHandler)) {
                    'The property handler was NOT removed (the administrator prompt was declined or failed).'
                    exit 1
                }
            }
        }
        if ($mark.DetailsSet -and (Test-Path $AssocKey)) {
            foreach ($n in $DetailValues) {
                if ($null -ne $mark."Prev_$n") { Set-ItemProperty $AssocKey -Name $n -Value $mark."Prev_$n" }
                else { Remove-ItemProperty $AssocKey -Name $n -ErrorAction SilentlyContinue }
            }
            $left = Get-Item $AssocKey
            if ($left.SubKeyCount -eq 0 -and $left.ValueCount -eq 0) { Remove-Item $AssocKey }
        }
        Remove-Item $MarkKey -Recurse -Force
        $parent = Get-Item 'HKCU:\Software\jxl-thumb' -ErrorAction SilentlyContinue
        if ($parent -and $parent.SubKeyCount -eq 0 -and $parent.ValueCount -eq 0) { Remove-Item 'HKCU:\Software\jxl-thumb' }
        'Explorer details for .jxl are back to how they were before enable_explorer_details.cmd.'
        exit 0
    }
}
