# 사용량 API·로그인 정보 확인 결과 (Task 4)

- 날짜: 2026-10-05, 사용자 동의 후 1회 호출. 토큰 값은 출력하지 않았다.
- 결론: 계획(Task 9)의 가정과 **같다**. 파서·테스트 수정 없음.

## Keychain `Claude Code-credentials`

- 최상위 키: `mcpOAuth`, `claudeAiOauth`, `organizationUuid`
- `claudeAiOauth`: `accessToken` (str), `refreshToken` (str), `expiresAt` (int, **epoch ms**), `refreshTokenExpiresAt` (int), `scopes` (str[]), `subscriptionType` (str, 값 `"max"`), `rateLimitTier` (str, 값 `"default_claude_max_5x"`)
- `FormatPlanLabel("max", "default_claude_max_5x")` → `Max 5x` 로 맞는다.

## `GET /api/oauth/usage` (HTTP 200)

- `five_hour`, `seven_day`: `{ utilization: number (0–100, 예 55.0), resets_at: "2026-10-05T06:50:00.180191+00:00" (ISO, 마이크로초 + 오프셋), limit_dollars, used_dollars, remaining_dollars, locked_reason }`
- 그 밖의 최상위 키: `seven_day_oauth_apps`, `seven_day_opus`, `seven_day_sonnet` 등 대부분 `null`, `extra_usage` (객체), `limits` (배열: `kind` session/weekly_all/weekly_scoped, `percent`, `severity`, `resets_at`, `is_active`), `spend`, 그리고 이름이 코드명인 실험용 키 다수.
- `resets_at` 의 마이크로초 6자리는 Task 9 의 `ParseTimestamp` 가 밀리초로 잘라 처리한다.

## fixture

`tests/fixtures/usage_response.json` 은 위 형태에서 값만 바꾸고(42.0 / 18.0) 코드명 키를 뺐다.
