#ifndef RATTLE_SOURCE_ONLY
#include "parser.hpp"
#include <memory>
#include <rattle/category.hpp>
#include <rattle/parser.hpp>
#include <rattle/parser_nodes.hpp>
#endif

namespace rattle::parser {
  static auto expect_eos(State &state) {
    auto ctx = state.with();
    auto token = state.get();
    switch (token.kind) {
    case lexer::Token::Kind::CloseBrace:
      state.unget(token);
    case lexer::Token::Kind::Newline:
    case lexer::Token::Kind::Semicolon:
    case lexer::Token::Kind::Eot:
      break;
    default:
      state.report(error_t::unterminated_statement, token);
      state.unget(token);
    }
    return token;
  }

  static auto expr_eos(State &state) {
    auto expr = parse_expression(state);
    expect_eos(state);
    return expr;
  }

  static auto continue_stmt(State &state, lexer::Token const &kwd) {
    return std::make_unique<nodes::Continue>((expect_eos(state), kwd));
  }

  static auto break_stmt(State &state, lexer::Token const &kwd) {
    return std::make_unique<nodes::Break>((expect_eos(state), kwd));
  }

  static auto return_stmt(State &state, lexer::Token const &kwd) {
    return std::make_unique<nodes::Return>(kwd, expr_eos(state));
  }

  static auto nonlocal_stmt(State &state, lexer::Token const &kwd) {
    return std::make_unique<nodes::NonLocal>(kwd, expr_eos(state));
  }

  static auto del_stmt(State &state, lexer::Token const &kwd) {
    return std::make_unique<nodes::Del>(kwd, expr_eos(state));
  }

  static auto global_stmt(State &state, lexer::Token const &kwd) {
    return std::make_unique<nodes::Global>(kwd, expr_eos(state));
  }

  static auto assert_stmt(State &state, lexer::Token const &kwd) {
    return std::make_unique<nodes::Assert>(kwd, expr_eos(state));
  }

  static auto raise_stmt(State &state, lexer::Token const &kwd) {
    return std::make_unique<nodes::Raise>(kwd, expr_eos(state));
  }

  static auto import_stmt(State &state, lexer::Token const &kwd) {
    return std::make_unique<nodes::Import>(kwd, expr_eos(state));
  }

  static std::unique_ptr<nodes::Expression> non_brace_expr(State &state) {
    auto token = state.get();
    state.unget(token);
    return token.kind != lexer::Token::Kind::OpenBrace ?
             parse_expression(state) :
             nullptr;
  }

  static auto block_stmt(State &state, lexer::Token const &brace) {
    auto ctx = state.with(State::IGNORE_EOSCOM);
    auto scope = state.enter_brace();
    std::vector<std::unique_ptr<nodes::Statement>> stmts;
    while (true) {
      if (state.empty()) {
        state.report(error_t::unterminated_brace, brace);
        break;
      }
      auto token = state.get();
      if (token.kind == lexer::Token::Kind::CloseBrace) {
        break;
      }
      state.unget(token);
      stmts.emplace_back(parse_statement(state));
    }
    stmts.shrink_to_fit();
    return std::make_unique<nodes::Block>(brace, std::move(stmts));
  }

  static auto from_stmt(State &state, lexer::Token const &kwd) {
    auto ctx = state.with();
    auto package = parse_expression(state);
    auto token = state.get();
    std::unique_ptr<nodes::Import> module;
    if (token.kind == lexer::Token::Kind::Import) {
      module = import_stmt(state, token);
    } else {
      state.unget(token);
    }
    return std::make_unique<nodes::From>(kwd, std::move(package),
                                         std::move(module));
  }

  static auto except_stmt(State &state, lexer::Token const &kwd) {
    auto ctx = state.with(State::IGNORE_NLCOM);
    auto captured = non_brace_expr(state);
    auto handler = parse_statement(state);
    return std::make_unique<nodes::Except>(kwd, std::move(captured),
                                           std::move(handler));
  }

  static auto lastly_stmt(State &state, lexer::Token const &kwd) {
    auto ctx = state.with(State::IGNORE_NLCOM);
    auto body = parse_statement(state);
    return std::make_unique<nodes::Lastly>(kwd, std::move(body));
  }

  static auto else_stmt(State &state, lexer::Token const &kwd) {
    auto ctx = state.with(State::IGNORE_NLCOM);
    return std::make_unique<nodes::Else>(kwd, parse_statement(state));
  }

  static auto try_stmt(State &state, lexer::Token const &kwd) {
    auto ctx = state.with(State::IGNORE_NLCOM);
    auto try_block = parse_statement(state);
    std::vector<std::unique_ptr<nodes::Except>> handlers;
    while (true) {
      auto token = state.get();
      if (token.kind == lexer::Token::Kind::Except) {
        handlers.emplace_back(except_stmt(state, token));
      } else {
        state.unget(token);
        break;
      }
    }
    auto token = state.get();
    std::unique_ptr<nodes::Else> else_block;
    if (token.kind == lexer::Token::Kind::Else) {
      else_block = else_stmt(state, token);
    } else {
      state.unget(token);
    }
    token = state.get();
    std::unique_ptr<nodes::Lastly> lastly_block;
    if (token.kind == lexer::Token::Kind::Lastly) {
      lastly_block = lastly_stmt(state, token);
    } else {
      state.unget(token);
    }
    return std::make_unique<nodes::Try>(
      kwd, std::move(try_block), std::move(handlers), std::move(else_block), std::move(lastly_block));
  }

  static auto with_stmt(State &state, lexer::Token const &kwd) {
    auto contexts = parse_expression(state);
    auto ctx = state.with(State::IGNORE_NLCOM);
    auto body = parse_statement(state);
    return std::make_unique<nodes::With>(kwd, std::move(contexts),
                                         std::move(body));
  }

  static auto for_stmt(State &state, lexer::Token const &kwd) {
    auto bind_expr = parse_expression(state);
    auto ctx = state.with(State::IGNORE_NLCOM);
    auto body = parse_statement(state);
    return std::make_unique<nodes::For>(kwd, std::move(bind_expr),
                                        std::move(body));
  }

  static auto while_stmt(State &state, lexer::Token const &kwd) {
    auto condition = parse_expression(state);
    auto ctx = state.with(State::IGNORE_NLCOM);
    auto body = parse_statement(state);
    return std::make_unique<nodes::While>(kwd, std::move(condition),
                                          std::move(body));
  }

  static auto if_stmt(State &state, lexer::Token const &kwd) {
    auto condition = parse_expression(state);
    auto ctx = state.with(State::IGNORE_NLCOM);
    auto if_block = parse_statement(state);
    auto token = state.get();
    std::unique_ptr<nodes::Else> else_block;
    if (token.kind == lexer::Token::Kind::Else) {
      else_block = else_stmt(state, token);
    } else {
      state.unget(token);
    }
    return std::make_unique<nodes::If>(kwd, std::move(condition),
                                       std::move(if_block), std::move(else_block));
  }

  static auto class_stmt(State &state, lexer::Token const &klass) {
    auto ctx = state.with(State::IGNORE_NLCOM);
    auto cls_decl = non_brace_expr(state);
    auto body = parse_statement(state);
    return std::make_unique<nodes::Class>(klass, std::move(cls_decl),
                                          std::move(body));
  }

  std::unique_ptr<nodes::Fn> fn_expression(State &state,
                                           lexer::Token const &fn) {
    auto decl = non_brace_expr(state);
    auto token = state.get();
    std::unique_ptr<nodes::Statement> body;
    if (token.kind == lexer::Token::Kind::OpenBrace) {
      body = block_stmt(state, token);
    } else {
      state.unget(token);
    }
    return std::make_unique<nodes::Fn>(fn, std::move(decl), std::move(body));
  }

  std::unique_ptr<nodes::Statement> assign_stmt(State &state) {
    auto assignable = parse_expression(state);
    auto ctx = state.with();
    auto token = state.get();
    switch (token.kind) {
#define TK_INCLUDE TK_ASSIGN
#define TK_MACRO(_Name, _)                                                     \
  case lexer::Token::Kind::_Name:                                              \
    return std::make_unique<nodes::_Name>(token, std::move(assignable),        \
                                          expr_eos(state));
#include <rattle/token_macro.hpp>
    default:
      state.unget(token);
      if (assignable) {
        return std::make_unique<nodes::ExprStatement>(expect_eos(state),
                                                      std::move(assignable));
      }
    }
    return nullptr;
  }

  std::unique_ptr<nodes::Statement> parse_statement(State &state) {
    auto ctx = state.with(State::IGNORE_EOSCOM);
    using Kind = lexer::Token::Kind;
    // clang-format off
    while (true) {
      auto token = state.get();
      switch(token.kind) {
        case Kind::Error:        break;
        case Kind::Eot:          return nullptr;
        case Kind::If:           return if_stmt(state, token);
        case Kind::Del:          return del_stmt(state, token);
        case Kind::Try:          return try_stmt(state, token);
        case Kind::For:          return for_stmt(state, token);
        case Kind::With:         return with_stmt(state, token);
        case Kind::Else:         return else_stmt(state, token);
        case Kind::From:         return from_stmt(state, token);
        case Kind::While:        return while_stmt(state, token);
        case Kind::OpenBrace:    return block_stmt(state, token);
        case Kind::Class:        return class_stmt(state, token);
        case Kind::Break:        return break_stmt(state, token);
        case Kind::Raise:        return raise_stmt(state, token);
        case Kind::Global:       return global_stmt(state, token);
        case Kind::Import:       return import_stmt(state, token);
        case Kind::Assert:       return assert_stmt(state, token);
        case Kind::Return:       return return_stmt(state, token);
        case Kind::Except:       return except_stmt(state, token);
        case Kind::Lastly:       return lastly_stmt(state, token);
        case Kind::NonLocal:     return nonlocal_stmt(state, token);
        case Kind::Continue:     return continue_stmt(state, token);
        case Kind::CloseParen:
          if (not state.in_paren())
            state.report(error_t::dangling_paren, token);
          break;
        case Kind::CloseBrace:
          if (not state.in_brace())
            state.report(error_t::dangling_brace, token);
          break;
        case Kind::CloseBracket:
          if (not state.in_bracket())
            state.report(error_t::dangling_bracket, token);
          break;
        default:
          state.unget(token);
          if(auto stmt = assign_stmt(state); stmt) return stmt;
          else return std::make_unique<nodes::Statement>(state.get());
      }
    }
    // clang-format on
  }
} // namespace rattle::parser
