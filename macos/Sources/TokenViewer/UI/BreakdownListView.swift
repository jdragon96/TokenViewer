import SwiftUI
import TokenViewerCore

/// The top five groups plus "기타 N개", each with a bar relative to the first row (v0.1.0 spec 2.2).
struct BreakdownListView: View {
    let breakdown: Breakdown
    /// The first scan of a long history takes seconds; say so instead of claiming there are no records.
    let isLoaded: Bool
    let fill: Color

    var body: some View {
        let rows = breakdown.displayRows
        let maxCost = breakdown.rows.first?.costUsd ?? 0
        VStack(spacing: 2) {
            if rows.isEmpty {
                Text(isLoaded ? "이 기간에 기록이 없습니다" : "로그를 읽는 중…")
                    .font(.system(size: 12))
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 8)
            }
            ForEach(Array(rows.enumerated()), id: \.offset) { _, row in
                BreakdownRowView(row: row, ratio: maxCost > 0 ? min(row.costUsd / maxCost, 1) : 0, fill: fill)
            }
        }
    }
}

private struct BreakdownRowView: View {
    let row: BreakdownRow
    let ratio: Double
    let fill: Color

    var body: some View {
        VStack(spacing: 3) {
            HStack(spacing: 8) {
                Text(row.label)
                    .font(.system(size: 12))
                    .lineLimit(1)
                    .truncationMode(.middle)
                Spacer(minLength: 0)
                Text(Formatters.amount(row))
                    .font(.system(size: 12))
                    .foregroundStyle(.secondary)
            }
            GeometryReader { proxy in
                ZStack(alignment: .leading) {
                    Capsule().fill(Color.primary.opacity(0.12))
                    if ratio > 0 {
                        Capsule().fill(fill).frame(width: max(proxy.size.width * ratio, 4))
                    }
                }
            }
            .frame(height: 4)
        }
        .frame(height: 28)
        .contentShape(Rectangle())
        .help(Formatters.tooltip(row))
    }
}
