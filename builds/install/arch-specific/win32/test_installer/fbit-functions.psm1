#region Helper functions #############

function show-help() {
  Get-Help $PSCommandPath -ShowWindow
}

function spacer() {
  param ( [string]$message )
  if ( $message ) {
    Write-Output ''
    Write-Output "=================== $message ==================="
    Write-Output ''
  } else {
    Write-Output '------------------------------------------------------------------------------------------'
  }
}

function RunTimeStamp() {
  Get-Date -UFormat '%Y-%m-%d_%H-%M-%S'
}

<#
.SYNOPSIS
Shorten the prompt

.DESCRIPTION
The path to the test installer script can get too long

.PARAMETER reset
Restore the full path

#>
function prompt( [switch]$reset ) {

  
  $local:identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $local:principal = [Security.Principal.WindowsPrincipal] $local:identity
  $local:adminRole = [Security.Principal.WindowsBuiltInRole]::Administrator

  $( if (Test-Path variable:/PSDebugContext) { '[DBG]: ' }
    elseif ($principal.IsInRole($adminRole)) { '[ADMIN]: ' }
    else { '' }
  ) + $( if ( $reset -eq $true ) {
      'PS ' + $(Get-Location) + $( if ( $NestedPromptLevel -ge 1 ) { '>>' } ) + '> '
    } else {
      'FBIT ' + '> '
    } )

}


function check_file_exists( [string]$_apath, [switch]$_isdir ) {
  if ( $_isdir ) {
    return ( (Test-Path -Path $_apath ) -ne ""  )
  } else {
    return ( ( Test-Path -Path $_apath -PathType Leaf  ) -ne "" )
  }
}


function Invoke-Command ($_commandPath, $_commandArguments, $_commandTitle, $_outfile ) {
  Try {
    $local:pinfo = New-Object System.Diagnostics.ProcessStartInfo
    $local:pinfo.FileName = "$_commandPath"
    $local:pinfo.RedirectStandardError = $true
    $local:pinfo.RedirectStandardOutput = $true
    $local:pinfo.UseShellExecute = $false
    $local:pinfo.Arguments = "$_commandArguments"
    $global:p = New-Object System.Diagnostics.Process
    $global:p.StartInfo = $pinfo
    $global:p.Start() | Out-Null
    $local:result_object = [pscustomobject]@{
      commandTitle = $_commandTitle
      stdout       = $p.StandardOutput.ReadToEnd()
      stderr       = $p.StandardError.ReadToEnd()
      ExitCode     = $p.ExitCode
    }
    $global:p.WaitForExit()
    if ( "$_outfile" -ne '' ) {
      $local:result_object.stdout > $_outfile
    } else {
      Write-Verbose $local:result_object.stdout
    }
    return $local:result_object.ExitCode
  } catch {
    Write-Output $local:result_object.stderr
    return $local:result_object.ExitCode

  }

  #  Write-Verbose "stdout: $stdout"
  #  Write-Verbose "stderr: $stderr"
  #  Write-Verbose "exit code:  $p.ExitCode"

}

<#
.SYNOPSIS
Load a simple config file and assign key-value pairs to $key variables

.DESCRIPTION


.EXAMPLE
# Comment lines may start with # or //
# Variables are strings by default
avar="AValue"
# Booleans must be passed as 0 or 1 and are converted to boolean types
aboolean=$true
# Switches may be set by passing the switch name prefixed with a hyphen
  -aswitch
# Or just passed 'as is'
  aswitch

.NOTES
General notes
#>
function LoadConfig( [string] $_conffile ) {
  
  foreach ($i in $(Get-Content $_conffile)) {
    # Trim any lead/trailing whitespace
    # Include value in quotes if you need trailing whitespace!
    $local:line = $i.trim()
    if ($local:line -ne '') {
      if ( -not ($line.startswith( '#' ) -or $line.startswith( '//' )) ) {

        $local:akey = $i.split('=')[0]
        if ( $null -ne $local:akey ) { $local:akey = $local:akey.trim() }  

        # Check if key is a switch.
        if ( $local:akey.StartsWith('-') ) { 
          Write-Verbose "$local:akey is a switch"
          Set-Variable -Scope global -Name $local:akey.TrimStart('-') -Value $true
          continue
        }

        $local:avalue = $i.split('=', 2)[1]
        if ( $null -ne $local:avalue ) { $local:avalue = $local:avalue.trim() }
        Write-Debug "akey is $local:akey and avalue is $local:avalue"
        # Test avalue and change type to boolean if necessary
        switch ( $local:avalue ) {
          { $_ -eq "1" } { Set-Variable -Scope global -Name $local:akey -Value $true }
          { $_ -eq "0" } { Set-Variable -Scope global -Name $local:akey -Value $false }
          { $_ -eq $null } { Set-Variable -Scope global -Name $local:akey -Value $true }
          Default { 
            Set-Variable -Scope global -Name $local:akey -Value $local:avalue
          }
        }
      }
    }
  }

}

#endregion end of helper functions #############


<#
.SYNOPSIS
Indicate if the (non-)existence of a file is a good or a bad thing.
.DESCRIPTION
When installing Firebird we expect certain files to exist.
When uninstalling we do not expect files to exist.

.PARAMETER afile
The file to check for

.PARAMETER str_if_true
The string to output if file exists. Defaults to 'good'

.PARAMETER str_if_false
The string to output if the file does not exist

.PARAMETER status_true_is_fail
When installing set status_true_is_fail to false.
When uninstalling set status_true_is_fail to true.

.PARAMETER isdir
Set isdir if testing for a directory.

.EXAMPLE
An example

.NOTES
General notes
#>
function check_file_status( $afile, [boolean]$status_true_is_fail, [boolean]$isdir
  , [string]$str_if_true = 'good', [string]$str_if_false = 'bad'
) {
  Write-Debug "Entering function $($MyInvocation.MyCommand.Name)"
  $local:retval = check_file_exists $afile $isdir
  if ( $local:retval -eq $true ) {
    Write-Output "$TAB $afile exists - $str_if_true"
  } else {
    Write-Output "$TAB $afile not found - $str_if_false"
  }

  if ( $status_true_is_fail -eq $true -and $local:retval -eq $true ) {
    Write-Verbose "$TAB $status_true_is_fail -eq $true -and $local:retval -eq $true "
    $ErrorCount += 1
  }

  if ( $status_true_is_fail -eq $false -and $local:retval -eq $false ) {
    Write-Verbose "$TAB $status_true_is_fail -eq $true -and $local:retval -eq $true "
    $ErrorCount += 1
  }
  Write-Debug "Leaving function $($MyInvocation.MyCommand.Name)"
}


function check_server_arch_configured() {
  Write-Debug "Entering function $($MyInvocation.MyCommand.Name)"

  if ( $script:classicserver ) { $local:str_to_test = 'servermode = classic' }
  if ( $script:superclassic ) { $local:str_to_test = 'servermode = superclassic' }
  if ( $script:superserver ) { $local:str_to_test = 'servermode = super' }

  # FIXME What if the fb.conf does not exist?
  $local:found = (Select-String -Path "$FIREBIRD/firebird.conf" -Pattern ^$local:str_to_test)

  if ( ! $local:found.Length -gt 0 ) {
    $ErrorCount += 1
    Write-Verbose $TAB $local:str_to_test not set in $FIREBIRD/firebird.conf

  }

  Write-Debug "Leaving function $($MyInvocation.MyCommand.Name)"
}


function check_service_installed( [string]$_servicename ) {

  $local:ExpectedServiceName = "FirebirdServerDefaultInstance"

  $local:ActualServiceName = Get-Service -ErrorAction ignore -Name $local:ExpectedServiceName | Select-Object -ExpandProperty Name
  if (check_result $local:ExpectedServiceName $local:ActualServiceName $true ) {
    $fbcol = ( $global:action -eq "check_install" ) ?  $global:fbgreen : $global:fbred
    Write-Host  -ForegroundColor $fbcol "${TAB}$local:ExpectedServiceName is installed. " 
  } else { 
    $fbcol = ( $global:action -eq "check_install" ) ?  $global:fbred : $global:fbgreen
    Write-Host  -ForegroundColor $fbcol "${TAB}$local:ExpectedServiceName is NOT installed. "  
  }
}

<#
.SYNOPSIS
Compare two strings

.DESCRIPTION
Long description

.PARAMETER expected
The result expected

.PARAMETER actual
The actual result

.PARAMETER equals_is_true
Set True if the actual result should equal the expected result
Or Set True if actualt result should not equal the expected result

.NOTES
General notes
#>
function check_result ( [string]$expected, [string]$actual, [boolean]$equals_is_true) {

  #   Write-Verbose "if ( ($expected -eq $actual ) -and $equals_is_true ){ return $true }"
  if ( ( "$expected" -eq "$actual" ) -and $equals_is_true ) { return $true }

  #   Write-Verbose "if ( ($expected -eq $actual ) -and !$equals_is_true ){ return $false }"
  if ( ( "$expected" -eq "$actual" ) -and ! $equals_is_true ) { return $false }

  #   Write-Verbose "if ( ($expected -ne $actual ) -and $equals_is_true ){ return $false }"
  if ( ( "$expected" -ne "$actual" ) -and $equals_is_true ) { return $false }

  #   Write-Verbose "if ( ($expected -ne $actual ) -and !$equals_is_true ){ return $true }"
  if ( ( "$expected" -ne "$actual" ) -and ! $equals_is_true ) { return $true }
}

function check_file_output( $afile, $apattern ) {
  $local:aretval = Select-String -Path $afile -Pattern $apattern -SimpleMatch -Quiet
  return $local:aretval

}

function print_output( $astring, [boolean]$found, [boolean]$found_is_fail
  , [string]$str_if_true = "GOOD", [string]$str_if_false = "BAD" ) {

  # If we find the string (ie result is not empty)
  if ( $found ) {
    if ( $found_is_fail ) {
      Write-Host -ForegroundColor Red "${TAB}$astring  - $str_if_false"
      $script:ErrorCount += 1
    } else {
      Write-Host -ForegroundColor Green "${TAB}$astring - $str_if_true"
    }
    # We did not find the string
  } else {
    if ( $found_is_fail ) {
      Write-Host -ForegroundColor Green "${TAB}$astring - $str_if_true"
    } else {
      Write-Host -ForegroundColor Red "${TAB}$astring - $str_if_false"
      $script:ErrorCount += 1
    }
  }

}



function print_vars( [string]$_action ) {
  Write-Debug "Entering function $($MyInvocation.MyCommand.Name)"

  $local:varfile = "$fbinstalllogdir/$testname-$_action-$run_timestamp.vars"

  if ( $PSCmdlet.MyInvocation.BoundParameters['Verbose'].IsPresent ) {
    spacer > $local:varfile
    if ( check_file_exists "fb_build_vars_${env:PROCESSOR_ARCHITECTURE}.txt" ) {
      spacer "Firebird Build Environment" >> $local:varfile
      Get-Content fb_build_vars_${env:PROCESSOR_ARCHITECTURE}.txt >> $local:varfile
      spacer "Firebird Build Environment END" >> $local:varfile
      spacer >> $local:varfile
    }
    spacer 'Global Vars' >> $local:varfile
    Get-Variable -Scope global >> $local:varfile
    spacer 'Global Vars END' >> $local:varfile
    spacer >> $local:varfile
    spacer 'Env Vars' >> $local:varfile
    env | grep '^FB' >> $local:varfile
    env | grep '^ISC' >> $local:varfile
    spacer 'Env Vars END' >> $local:varfile
    spacer 'Script Vars' >> $local:varfile
    Get-Variable -Scope script | Format-Table -AutoSize -Wrap >> $local:varfile
    spacer 'Script Vars END' >> $local:varfile
    spacer >> $local:varfile
    spacer 'Local Vars' >> $local:varfile
    Get-Variable -Scope local >> $local:varfile
    spacer 'Local Vars END' >> $local:varfile
    spacer >> $local:varfile
  }
  Write-Debug "Leaving $($MyInvocation.MyCommand.Name)"

}

<#
.SYNOPSIS
Execute SQL via isql.exe

.NOTES
This function assumes that the script to execute exists in $env:Temp\infile.txt
and the output will be stored in $env:Temp\outfile.txt
#>
function Exec_SQL( [string]$db = "localhost:employee", 
  [string]$username = "sysdba", [string]$pw = "masterkey" ) {

  # Always reset outfile otherwise output is appended.
  Write-Output "" > $env:Temp\outfile.txt

  Invoke-Command "$script:FIREBIRD\isql.exe" " -user $username -password $pw -z `
  -i $env:Temp/infile.txt -o $env:Temp/outfile.txt -m -m2 $db"

}  