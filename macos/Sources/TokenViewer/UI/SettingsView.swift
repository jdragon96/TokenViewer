import SwiftUI
import TokenViewerCore

/// The settings window (v0.1.0 spec 2.3). Changes go straight into AppState; AppController saves and applies them.
struct SettingsView: View {
    let controller: AppController

    var body: some View {
        @Bindable var state = controller.state
        let step = SettingsStore.percentStep
        Form {
            Picker("한도 갱신 주기", selection: $state.settings.intervalMinutes) {
                ForEach(SettingsStore.allowedIntervals, id: \.self) { Text("\($0)분").tag($0) }
            }
            Text("드롭다운을 열 때 30초가 지났으면 바로 갱신합니다.")
                .font(.system(size: 11))
                .foregroundStyle(.secondary)
            Picker("주황 경고", selection: $state.settings.warnPercent) {
                ForEach(Array(stride(from: SettingsStore.minWarn, through: SettingsStore.maxWarn, by: step)), id: \.self) {
                    Text("\($0)%").tag($0)
                }
            }
            Picker("빨강 경고", selection: $state.settings.criticalPercent) {
                ForEach(Array(stride(from: state.settings.warnPercent + step, through: SettingsStore.maxPercent, by: step)), id: \.self) {
                    Text("\($0)%").tag($0)
                }
            }
            Toggle("로그인 시 자동 실행", isOn: $state.settings.launchAtLogin)
            HStack {
                Text("v\(AppInfo.version)")
                    .foregroundStyle(.secondary)
                Spacer()
                if let url = AppInfo.repositoryURL {
                    Link("GitHub", destination: url)
                }
            }
        }
        .formStyle(.grouped)
        .frame(width: 380)
        .fixedSize(horizontal: false, vertical: true)
    }
}
