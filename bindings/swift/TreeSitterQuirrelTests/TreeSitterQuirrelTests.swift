import XCTest
import SwiftTreeSitter
import TreeSitterQuirrel

final class TreeSitterQuirrelTests: XCTestCase {
    func testCanLoadGrammar() throws {
        let parser = Parser()
        let language = Language(language: tree_sitter_quirrel())
        XCTAssertNoThrow(try parser.setLanguage(language),
                         "Error loading Quirrel grammar")
    }
}
