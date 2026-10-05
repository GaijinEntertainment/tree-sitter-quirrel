package tree_sitter_quirrel_test

import (
	"testing"

	tree_sitter "github.com/tree-sitter/go-tree-sitter"
	tree_sitter_quirrel "github.com/GaijinEntertainment/tree-sitter-quirrel/bindings/go"
)

func TestCanLoadGrammar(t *testing.T) {
	parser := tree_sitter.NewParser()
	defer parser.Close()

	if err := parser.SetLanguage(tree_sitter.NewLanguage(tree_sitter_quirrel.Language())); err != nil {
		t.Errorf("Error loading Quirrel grammar: %v", err)
	}
}
