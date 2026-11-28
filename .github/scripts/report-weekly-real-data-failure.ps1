$title = 'Weekly real-data run failed'
$run = "$env:GITHUB_SERVER_URL/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID"
$failedLog = if ($env:REAL_DATA_BUILD_DIR) { Join-Path $env:REAL_DATA_BUILD_DIR 'Testing\Temporary\LastTestsFailed.log' } else { '' }
$tests = if ($env:TEST_OUTCOME -eq 'failure' -and $failedLog -and (Test-Path -LiteralPath $failedLog -PathType Leaf)) {
  @(Get-Content -LiteralPath $failedLog | ForEach-Object { '- `' + ($_ -replace '^\d+:', '') + '`' })
} else { @('No failed-test list: the run stopped before or outside ctest. See the run log.') }
$body = @("The Sunday real-data run on main ($($env:GITHUB_SHA.Substring(0, 12))) failed.", '', "Run: $run", '', 'Failed tests:') + $tests + @('', "<!-- weekly-real-data:$($env:GITHUB_RUN_ID):$($env:GITHUB_RUN_ATTEMPT):failed -->")
$file = Join-Path $env:RUNNER_TEMP 'real-data-report.md'
[IO.File]::WriteAllText($file, ($body -join "`n"), (New-Object System.Text.UTF8Encoding $false))
$matches = gh issue list --repo $env:GITHUB_REPOSITORY --state open --limit 1000 --json number,title | ConvertFrom-Json
if ($LASTEXITCODE -ne 0) { exit 1 }
$open = @($matches | Where-Object { $_.title -ceq $title } | Select-Object -First 1 -ExpandProperty number)
if ($open) { gh issue comment $open --repo $env:GITHUB_REPOSITORY --body-file $file } else { gh issue create --repo $env:GITHUB_REPOSITORY --title $title --body-file $file }
exit $LASTEXITCODE
