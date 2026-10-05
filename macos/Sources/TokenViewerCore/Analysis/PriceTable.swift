import Foundation

public struct ModelPrice: Sendable, Equatable {
    public var id: String
    public var family: String
    public var input: Double
    public var output: Double
    public var cacheWrite5m: Double
    public var cacheWrite1h: Double
    public var cacheRead: Double
    public var fastMultiplier: Double
}

public enum PriceMatch: Sendable, Equatable {
    case exact
    case family
    case none
}

public struct CostResult: Sendable, Equatable {
    public var usd: Double
    public var isApproximate: Bool
    public var isUnpriced: Bool

    public init(usd: Double = 0, isApproximate: Bool = false, isUnpriced: Bool = false) {
        self.usd = usd
        self.isApproximate = isApproximate
        self.isUnpriced = isUnpriced
    }
}

/// USD per 1M tokens from `shared/prices.json` (v0.1.0 spec 5.5).
public struct PriceTable: Sendable {
    private static let tokensPerPriceUnit = 1_000_000.0

    public let asOf: String
    public let models: [ModelPrice]

    /// Nil when the JSON has no model with an id.
    public init?(json: Data) {
        guard let root = (try? JSONSerialization.jsonObject(with: json)) as? [String: Any],
              let entries = root["models"] as? [[String: Any]] else {
            return nil
        }
        let models = entries.compactMap { entry -> ModelPrice? in
            guard let id = entry["id"] as? String, !id.isEmpty else {
                return nil
            }
            func price(_ key: String, default fallback: Double = 0) -> Double {
                JSONValues.number(entry[key]) ?? fallback
            }
            return ModelPrice(id: id, family: entry["family"] as? String ?? "",
                              input: price("input"), output: price("output"),
                              cacheWrite5m: price("cacheWrite5m"), cacheWrite1h: price("cacheWrite1h"),
                              cacheRead: price("cacheRead"), fastMultiplier: price("fastMultiplier", default: 1))
        }
        guard !models.isEmpty else {
            return nil
        }
        self.models = models
        asOf = root["asOf"] as? String ?? ""
    }

    public func findPrice(for modelId: String) -> (match: PriceMatch, price: ModelPrice?) {
        for price in models where modelId == price.id || Self.isDatedVariant(modelId, of: price.id) {
            return (.exact, price)
        }
        let parts = modelId.split(separator: "-").map(String.init)
        for price in models where !price.family.isEmpty && parts.contains(price.family) {
            return (.family, price)
        }
        return (.none, nil)
    }

    public func cost(of record: TokenRecord) -> CostResult {
        let quote = findPrice(for: record.model)
        guard let price = quote.price else {
            return CostResult(isUnpriced: true)
        }
        let weighted = Double(record.input) * price.input
            + Double(record.output) * price.output
            + Double(record.cacheWrite5m) * price.cacheWrite5m
            + Double(record.cacheWrite1h) * price.cacheWrite1h
            + Double(record.cacheRead) * price.cacheRead
        var usd = weighted / Self.tokensPerPriceUnit
        if record.isFast {
            usd *= price.fastMultiplier
        }
        return CostResult(usd: usd, isApproximate: quote.match == .family)
    }

    /// `claude-haiku-4-5-20251001` is the dated release of `claude-haiku-4-5`.
    private static func isDatedVariant(_ modelId: String, of id: String) -> Bool {
        guard modelId.hasPrefix(id) else {
            return false
        }
        let suffix = modelId.dropFirst(id.count)
        return suffix.count == 9 && suffix.first == "-" && suffix.dropFirst().allSatisfy { $0.isASCII && $0.isNumber }
    }
}
