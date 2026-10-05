import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct StatusTextTests {
    private let now = TestSupport.date("2026-10-05T12:00:00Z")

    private func status(_ results: (FetchResult, TimeInterval)...) -> UsageStatus {
        var status = UsageStatus()
        for (result, offset) in results {
            status.apply(result, now: now.addingTimeInterval(offset))
        }
        return status
    }

    @Test func loadingBeforeFirstAttempt() {
        #expect(StatusText.notice(UsageStatus(), now: now) == "한도를 불러오는 중…")
        #expect(StatusText.footer(UsageStatus(), now: now) == StatusText.Footer(text: "한도 정보 없음", isFailure: false))
    }

    @Test func okHasNoNotice() {
        #expect(StatusText.notice(status((FetchResult(status: .ok), 0)), now: now) == "")
    }

    @Test func noticeExplainsEachFailure() {
        #expect(StatusText.notice(status((FetchResult(status: .notLoggedIn), 0)), now: now)
                == "Claude Code 로그인 정보를 찾지 못했습니다.\n터미널에서 claude 실행 후 /login 하세요.")
        #expect(StatusText.notice(status((FetchResult(status: .keychainDenied), 0)), now: now)
                == "Keychain 접근이 거부되었습니다.\n'다시 확인'을 누르면 허용 창이 다시 뜹니다.")
        #expect(StatusText.notice(status((FetchResult(status: .tokenExpired), 0)), now: now)
                == "로그인 토큰이 만료되었습니다. Claude Code 를 실행하면 갱신됩니다.")
        #expect(StatusText.notice(status((FetchResult(status: .badResponse), 0)), now: now)
                == "사용량 API 응답 형식이 바뀌었습니다.\n~/Library/Logs/TokenViewer 를 확인하세요.")
    }

    @Test func noticeShowsAgeOfLastValue() {
        let failing = FetchResult(status: .serverError, detail: "서버 오류 (HTTP 503)")
        #expect(StatusText.notice(status((FetchResult(status: .ok), 0), (failing, 18 * 60)), now: now.addingTimeInterval(18 * 60))
                == "18분 전 값입니다. 갱신 실패: 서버 오류 (HTTP 503)")
        #expect(StatusText.notice(status((failing, 0)), now: now) == "갱신 실패: 서버 오류 (HTTP 503)")
    }

    @Test func footerShowsAgeOrFailure() {
        let ok = status((FetchResult(status: .ok), 0))
        #expect(StatusText.footer(ok, now: now.addingTimeInterval(5 * 60)) == StatusText.Footer(text: "5분 전 업데이트", isFailure: false))
        let failing = status((FetchResult(status: .ok), 0), (FetchResult(status: .networkError), 60))
        #expect(StatusText.footer(failing, now: now.addingTimeInterval(6 * 60)) == StatusText.Footer(text: "갱신 실패 · 6분 전", isFailure: true))
        #expect(StatusText.footer(status((FetchResult(status: .networkError), 0)), now: now) == StatusText.Footer(text: "한도 정보 없음", isFailure: true))
    }
}
