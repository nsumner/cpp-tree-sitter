// Fixture grammar for newer features and typed-dispatch tests.
module.exports = grammar({
  name: 'ctstest',
  extras: $ => [/\s/],
  supertypes: $ => [$._expr, $._literal, $._numeric, $._stringy, $._scalar],
  rules: {
    source: $ => repeat(choice($._expr, $.assignment, $.pair, $.index)),
    _expr: $ => choice($._literal, $.binary, $.negation, $.paren),
    _literal: $ => choice($._numeric, $.string),
    _numeric: $ => choice($.number, $.float),
    _stringy: $ => choice($.string, $.identifier),
    _scalar: $ => choice($._numeric, $.identifier),
    assignment: $ => seq($.identifier, '=', $._stringy),
    pair: $ => seq(alias($.identifier, $.key), ':', $._literal),
    index: $ => seq('[', $._scalar, ']'),
    binary: $ => prec.left(1, seq($._expr, '+', $._expr)),
    negation: $ => prec(2, seq(alias('-', $.minus), $._expr)),
    paren: $ => seq('(', $._expr, ')'),
    number: $ => /[0-9]+/,
    float: $ => /[0-9]+\.[0-9]+/,
    string: $ => /"[^"]*"/,
    identifier: $ => /[a-z]+/,
  }
});
