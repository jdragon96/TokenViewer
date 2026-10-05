import SwiftUI

/// One limit meter: name and %, a 6 pt bar, and the reset time (v0.1.0 spec 2.2).
struct LimitMeterView: View {
    let name: String
    let value: String
    let percent: Double
    let subtitle: String
    let fill: Color
    let isDimmed: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(name)
                    .font(.system(size: 12))
                    .foregroundStyle(.secondary)
                Spacer()
                Text(value)
                    .font(.system(size: 15, weight: .bold))
                    .foregroundStyle(isDimmed ? Color.secondary : Color.primary)
            }
            GeometryReader { proxy in
                ZStack(alignment: .leading) {
                    Capsule().fill(Color.primary.opacity(0.12))
                    if percent > 0 {
                        Capsule()
                            .fill(fill.opacity(isDimmed ? 0.45 : 1))
                            .frame(width: max(proxy.size.width * min(percent, 100) / 100, 6))
                    }
                }
            }
            .frame(height: 6)
            Text(subtitle)
                .font(.system(size: 11))
                .foregroundStyle(.tertiary)
        }
        .frame(maxWidth: .infinity)
    }
}
