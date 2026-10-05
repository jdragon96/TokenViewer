import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct PriceTableTests {
    private func record(_ model: String, fast: Bool = false) -> TokenRecord {
        TokenRecord(key: "k", timestamp: Date(timeIntervalSince1970: 0), model: model, isFast: fast,
                    input: 1_000_000, output: 100_000, cacheWrite5m: 200_000, cacheWrite1h: 50_000, cacheRead: 2_000_000)
    }

    @Test func loadsSharedPrices() throws {
        let prices = try TestSupport.prices()
        #expect(prices.models.count == 7)
        #expect(prices.asOf == "2026-09-25")
    }

    @Test func findsExactModel() throws {
        let quote = try TestSupport.prices().findPrice(for: "claude-opus-5-5")
        #expect(quote.match == .exact)
        #expect(quote.price?.input == 4)
        #expect(quote.price?.cacheRead == 0.2)
    }

    @Test func findsDatedModel() throws {
        let quote = try TestSupport.prices().findPrice(for: "claude-haiku-4-5-20251001")
        #expect(quote.match == .exact)
        #expect(quote.price?.output == 5)
    }

    @Test func fallsBackToFamily() throws {
        let prices = try TestSupport.prices()
        #expect(prices.findPrice(for: "claude-opus-6").match == .family)
        #expect(prices.findPrice(for: "claude-opus-6").price?.id == "claude-opus-5-5")
        // "claude-opus-5" is a prefix of this id, but the rest is not a date suffix.
        #expect(prices.findPrice(for: "claude-opus-5-7").match == .family)
    }

    @Test func unknownFamilyIsUnpriced() throws {
        let prices = try TestSupport.prices()
        #expect(prices.findPrice(for: "gpt-5").match == .none)
        #expect(prices.cost(of: record("gpt-5")) == CostResult(isUnpriced: true))
    }

    @Test func calculatesCost() throws {
        // 1M×4 + 0.1M×20 + 0.2M×5 + 0.05M×8 + 2M×0.2 = 7.8
        let cost = try TestSupport.prices().cost(of: record("claude-opus-5-5"))
        #expect(abs(cost.usd - 7.8) < 1e-9)
        #expect(!cost.isApproximate)
        #expect(!cost.isUnpriced)
    }

    @Test func doublesFastOpus() throws {
        let cost = try TestSupport.prices().cost(of: record("claude-opus-5-5", fast: true))
        #expect(abs(cost.usd - 15.6) < 1e-9)
    }

    @Test func ignoresFastWithoutMultiplier() throws {
        // 1M×2 + 0.1M×10 + 0.2M×2.5 + 0.05M×4 + 2M×0.2 = 4.1
        let cost = try TestSupport.prices().cost(of: record("claude-sonnet-5-5", fast: true))
        #expect(abs(cost.usd - 4.1) < 1e-9)
    }

    @Test func marksFamilyCostApproximate() throws {
        let cost = try TestSupport.prices().cost(of: record("claude-opus-6"))
        #expect(cost.isApproximate)
        #expect(abs(cost.usd - 7.8) < 1e-9)
    }

    @Test func rejectsBrokenJson() {
        #expect(PriceTable(json: Data("{".utf8)) == nil)
        #expect(PriceTable(json: Data(#"{"models": []}"#.utf8)) == nil)
        #expect(PriceTable(json: Data(#"{"models": [{"family": "opus"}]}"#.utf8)) == nil)
    }

    @Test func defaultsMissingFastMultiplierToOne() {
        let prices = PriceTable(json: Data(#"{"models": [{"id": "claude-x-1", "family": "x", "input": 1}]}"#.utf8))
        #expect(prices?.models.first?.fastMultiplier == 1)
        #expect(prices?.models.first?.output == 0)
    }
}
