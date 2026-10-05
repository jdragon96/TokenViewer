import SwiftUI
import TokenViewerCore

/// The dropdown (v0.1.0 spec 2.2): header, limits, notice, where the tokens went, footer.
struct PopupView: View {
    let controller: AppController
    @Environment(\.colorScheme) private var colorScheme

    private var state: AppState { controller.state }
    private var appearance: MenuBarAppearance { colorScheme == .dark ? .dark : .light }

    var body: some View {
        @Bindable var bindable = controller.state
        VStack(alignment: .leading, spacing: 8) {
            header
            if state.displayState != .empty {
                meters
            }
            notice
            if state.usage.needsRetryButton {
                Button("다시 확인") { controller.refreshNow() }
            }
            Divider()
            Picker("기준", selection: $bindable.settings.dimension) {
                ForEach(BreakdownDimension.allCases, id: \.self) { Text($0.title).tag($0) }
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            BreakdownListView(breakdown: state.breakdown, isLoaded: state.isBreakdownLoaded,
                              fill: Palette.meter(.normal, appearance).color)
            Picker("기간", selection: $bindable.settings.period) {
                ForEach(BreakdownPeriod.allCases, id: \.self) { Text($0.title).tag($0) }
            }
            .pickerStyle(.menu)
            .labelsHidden()
            .fixedSize()
            Divider()
            footer
        }
        .padding(EdgeInsets(top: 11, leading: 14, bottom: 10, trailing: 14))
        .frame(width: 300)
        .background(RoundedRectangle(cornerRadius: 12).fill(Color(nsColor: .windowBackgroundColor)))
        .overlay(RoundedRectangle(cornerRadius: 12).strokeBorder(Color.primary.opacity(0.12), lineWidth: 1))
    }

    private var header: some View {
        HStack(spacing: 6) {
            Text("Claude 사용량")
                .font(.system(size: 13, weight: .bold))
            if !state.usage.planLabel.isEmpty {
                Text(state.usage.planLabel)
                    .font(.system(size: 11))
                    .padding(.horizontal, 5)
                    .overlay(RoundedRectangle(cornerRadius: 5).strokeBorder(Color.secondary.opacity(0.5)))
            }
            Spacer()
        }
    }

    private var meters: some View {
        let limits = state.usage.limits
        let isDimmed = state.displayState == .stale
        return HStack(spacing: 14) {
            meter("5시간", limits?.fiveHour, subtitle: Formatters.resetCountdown(limits?.fiveHour?.resetsAt, now: state.now), isDimmed: isDimmed)
            meter("주간", limits?.sevenDay, subtitle: Formatters.resetDay(limits?.sevenDay?.resetsAt, now: state.now), isDimmed: isDimmed)
        }
    }

    private func meter(_ name: String, _ window: LimitWindow?, subtitle: String, isDimmed: Bool) -> LimitMeterView {
        guard let window else {
            return LimitMeterView(name: name, value: "—", percent: 0, subtitle: "정보 없음",
                                  fill: Palette.meter(.normal, appearance).color, isDimmed: isDimmed)
        }
        let level = StatusIconRenderer.level(percent: window.percent, warn: state.settings.warnPercent, critical: state.settings.criticalPercent)
        return LimitMeterView(name: name, value: Formatters.percent(window.percent), percent: window.percent, subtitle: subtitle,
                              fill: Palette.meter(level, appearance).color, isDimmed: isDimmed)
    }

    @ViewBuilder private var notice: some View {
        let text = StatusText.notice(state.usage, now: state.now)
        if !text.isEmpty {
            Text(text)
                .font(.system(size: 12))
                .fixedSize(horizontal: false, vertical: true)
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(8)
                .background(RoundedRectangle(cornerRadius: 8).fill(Color.primary.opacity(0.08)))
        }
    }

    private var footer: some View {
        let footer = StatusText.footer(state.usage, now: state.now)
        return HStack(spacing: 4) {
            Text(footer.text)
                .font(.system(size: 11))
                .foregroundStyle(footer.isFailure ? Palette.level(.critical, appearance).color : Color.secondary)
            Spacer()
            Button("↻") { controller.refreshNow() }
                .help("새로고침")
            Button("⚙") { controller.openSettings() }
                .help("설정")
            Button("종료") { controller.quit() }
        }
        .buttonStyle(.borderless)
    }
}
