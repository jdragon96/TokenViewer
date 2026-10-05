import Foundation

/// Groups records by project, model or session and ranks them by API-equivalent cost (v0.1.0 spec 5.3–5.5).
public enum TokenAggregator {
    public static let defaultTopCount = 5
    private static let unknownProject = "(알 수 없음)"
    private static let fiveHours: TimeInterval = 5 * 3600
    private static let day: TimeInterval = 86_400

    private struct Group {
        var row = BreakdownRow()
        var pricedCount = 0
        var unpricedCount = 0
        var firstSeen: Date?
        var projectPath = ""
    }

    public static func aggregate(_ snapshot: LogSnapshot, prices: PriceTable?, dimension: BreakdownDimension, from start: Date?,
                                 topCount: Int = defaultTopCount, timeZone: TimeZone = .current) -> Breakdown {
        let records = snapshot.records.filter { record in
            guard let start else {
                return true
            }
            return record.timestamp >= start
        }
        let projectLabels = projectLabels(for: records)

        var groups: [String: Group] = [:]
        for record in records {
            let key = switch dimension {
            case .project: record.projectPath
            case .model: record.model
            case .session: record.sessionId
            }
            var group = groups[key, default: Group()]
            let cost = prices?.cost(of: record) ?? CostResult(isUnpriced: true)
            group.row.costUsd += cost.usd
            group.row.isApproximate = group.row.isApproximate || cost.isApproximate
            if cost.isUnpriced {
                group.unpricedCount += 1
            } else {
                group.pricedCount += 1
            }
            group.row.input += record.input
            group.row.output += record.output
            group.row.cacheWrite += record.cacheWrite5m + record.cacheWrite1h
            group.row.cacheRead += record.cacheRead
            if group.firstSeen.map({ record.timestamp < $0 }) ?? true {
                group.firstSeen = record.timestamp
                group.projectPath = record.projectPath
            }
            groups[key] = group
        }

        let sessionTime = DateFormatter()
        sessionTime.locale = Locale(identifier: "en_US_POSIX")
        sessionTime.timeZone = timeZone
        sessionTime.dateFormat = "M/d HH:mm"

        var rows = groups.map { key, group -> BreakdownRow in
            var row = group.row
            row.detail = key
            row.isUnpriced = group.pricedCount == 0
            row.isApproximate = row.isApproximate || (group.pricedCount > 0 && group.unpricedCount > 0)
            switch dimension {
            case .project:
                row.label = labelOrUnknown(projectLabels[key])
            case .model:
                row.label = modelDisplayName(key)
            case .session:
                if let title = snapshot.sessionTitles[key], !title.isEmpty {
                    row.label = title
                } else {
                    let started = group.firstSeen.map { sessionTime.string(from: $0) } ?? ""
                    row.label = labelOrUnknown(projectLabels[group.projectPath]) + " · " + started
                }
            }
            return row
        }
        rows.sort(by: isRankedBefore)

        var breakdown = Breakdown()
        for (index, row) in rows.enumerated() {
            breakdown.totalUsd += row.costUsd
            if index < topCount {
                breakdown.rows.append(row)
            } else {
                add(row, to: &breakdown.otherRow)
                breakdown.otherCount += 1
            }
        }
        if breakdown.otherCount > 0 {
            breakdown.otherRow.label = "기타 \(breakdown.otherCount)개"
        }
        return breakdown
    }

    public static func periodStart(_ period: BreakdownPeriod, now: Date, fiveHourResetsAt: Date?, calendar: Calendar = .current) -> Date {
        switch period {
        case .fiveHourWindow:
            // A reset time that has already passed belongs to the previous window.
            if let resetsAt = fiveHourResetsAt, resetsAt > now {
                return resetsAt.addingTimeInterval(-fiveHours)
            }
            return now.addingTimeInterval(-fiveHours)
        case .today:
            return calendar.startOfDay(for: now)
        case .sevenDays:
            return now.addingTimeInterval(-7 * day)
        case .thirtyDays:
            return now.addingTimeInterval(-30 * day)
        }
    }

    /// `claude-opus-5-5` → `Opus 5.5`; ids that do not look like Claude models stay as they are.
    public static func modelDisplayName(_ modelId: String) -> String {
        guard let match = modelId.wholeMatch(of: #/claude-([a-z]+)((?:-\d{1,2})+)(?:-\d{8})?/#) else {
            return modelId
        }
        let family = String(match.1)
        let version = match.2.dropFirst().replacingOccurrences(of: "-", with: ".")
        return "\(family.prefix(1).uppercased())\(family.dropFirst()) \(version)"
    }

    private static func projectLabels(for records: [TokenRecord]) -> [String: String] {
        var pathsByName: [String: Set<String>] = [:]
        for record in records {
            pathsByName[lastComponent(record.projectPath), default: []].insert(record.projectPath)
        }
        var labels: [String: String] = [:]
        for (name, paths) in pathsByName {
            for path in paths {
                // Two projects share a folder name: prefix the parent folder to tell them apart.
                labels[path] = paths.count == 1 ? name : lastComponent((path as NSString).deletingLastPathComponent) + "/" + name
            }
        }
        return labels
    }

    private static func lastComponent(_ path: String) -> String {
        (path as NSString).lastPathComponent
    }

    private static func labelOrUnknown(_ label: String?) -> String {
        guard let label, !label.isEmpty else {
            return unknownProject
        }
        return label
    }

    private static func isRankedBefore(_ lhs: BreakdownRow, _ rhs: BreakdownRow) -> Bool {
        if lhs.costUsd != rhs.costUsd {
            return lhs.costUsd > rhs.costUsd
        }
        if lhs.totalTokens != rhs.totalTokens {
            return lhs.totalTokens > rhs.totalTokens
        }
        return lhs.label < rhs.label
    }

    private static func add(_ source: BreakdownRow, to target: inout BreakdownRow) {
        target.costUsd += source.costUsd
        target.isApproximate = target.isApproximate || source.isApproximate || source.isUnpriced
        target.input += source.input
        target.output += source.output
        target.cacheWrite += source.cacheWrite
        target.cacheRead += source.cacheRead
    }
}
