#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "integration/integration_support.hpp"
#include "shashoku/shashoku.hpp"

// `white-space: nowrap`（CSS Text Level 3 §5.1。ARCHITECTURE.md A58）を、本物のフォントを
// 通した end-to-end で確かめる。動機は「図のラベルが折れる」という実際の依頼
// （docs/benchmark/2026-09-25-skill/pipeline.html の 6 段パイプライン図。段の名前
// 「スタイル付きツリー」が `--width 1100` で「スタイル付きツ / リー」に折れていた）。
//
// 折り返さない結果として幅を超えることはあるので、**はみ出しの診断とセット**で見る
// （fail loudly の維持。DESIGN.md §3-6）: 紙面から出れば `content-overflow` の警告になり、
// `warnings_as_errors` で失敗する。
namespace shashoku::test {
namespace {

// --dump-stage box の JSON から行ごとのテキストを取る（line_policy_test.cpp と同じ読み方）。
// `"fragments"` は LineBox 1 つにつきちょうど 1 回出るので行の区切りに使える。
std::vector<std::string> line_texts(std::string_view json) {
  constexpr std::string_view kLineMarker = R"("fragments")";
  constexpr std::string_view kTextKey = R"("text": ")";

  std::vector<std::string> lines;
  for (std::size_t at = json.find(kLineMarker); at != std::string_view::npos;
       at = json.find(kLineMarker, at + 1)) {
    const std::size_t stop = json.find(kLineMarker, at + 1);
    std::string current;
    for (std::size_t text_at = json.find(kTextKey, at);
         text_at != std::string_view::npos && text_at < stop;
         text_at = json.find(kTextKey, text_at + 1)) {
      const std::size_t begin = text_at + kTextKey.size();
      const std::size_t end = json.find('"', begin);
      if (end == std::string_view::npos) {
        break;
      }
      current += json.substr(begin, end - begin);
    }
    lines.push_back(current);
  }
  return lines;
}

std::vector<std::string> lines_of(std::string_view html, int width) {
  const auto json = dump(html, japanese_fonts(), ImageSet{}, options_for(width), DumpStage::Box);
  EXPECT_TRUE(json.has_value()) << to_string(json.error());
  if (!json) {
    return {};
  }
  return line_texts(*json);
}

bool has_line(const std::vector<std::string>& lines, std::string_view text) {
  return std::ranges::find(lines, text) != lines.end();
}

// 受け入れケースの図（docs/benchmark/2026-09-25-skill/pipeline.html の `.flow` の部分）。
// `nowrap` を `.name` に足すかどうかだけが違う 2 本を作る。
std::string pipeline_html(bool nowrap) {
  return std::string(R"(<style>
  * { box-sizing: border-box; }
  .wrap  { padding: 24px 28px; background: #ffffff; color: #1b2733; }
  .flow  { display: flex; align-items: center; gap: 8px; }
  .step  { flex: 1 1 0; display: flex; flex-direction: column; gap: 6px;
           padding: 12px 12px; border-radius: 10px; background: #f1f4f9; }
  .no    { font-size: 11px; color: #6b7a90; letter-spacing: 1px; }
  .name  { font-size: 14px; font-weight: bold; line-height: 1.4;)") +
         (nowrap ? " white-space: nowrap;" : "") + R"( }
  .arrow { flex: none; font-size: 20px; color: #9aa7b8; }
</style>
<div class="wrap">
  <div class="flow">
    <div class="step"><div class="no">入力</div><div class="name">HTML 断片</div></div>
    <div class="arrow">→</div>
    <div class="step"><div class="no">① html</div><div class="name">DOM</div></div>
    <div class="arrow">→</div>
    <div class="step"><div class="no">② style</div><div class="name">スタイル付きツリー</div></div>
    <div class="arrow">→</div>
    <div class="step"><div class="no">③ layout</div><div class="name">ボックスツリー</div></div>
    <div class="arrow">→</div>
    <div class="step"><div class="no">④⑤ paint</div><div class="name">描画リストと Bitmap</div></div>
    <div class="arrow">→</div>
    <div class="step"><div class="no">⑥ png</div><div class="name">PNG</div></div>
  </div>
</div>)";
}

// 段の名前（`.name` の中身）。すべて 1 行に収まっていることを確かめる。
constexpr std::array kNames = {"HTML 断片",           "DOM", "スタイル付きツリー", "ボックスツリー",
                               "描画リストと Bitmap", "PNG"};

// ---- 受け入れケース ---------------------------------------------------------------

// A58 の動機そのもの: 幅 1100 で段の名前が折れない。
TEST(Nowrap, PipelineDiagramLabelsStayOnOneLine) {
  const std::vector<std::string> as_written = lines_of(pipeline_html(false), 1100);
  const std::vector<std::string> with_nowrap = lines_of(pipeline_html(true), 1100);

  // 修正前の症状: いちばん長い名前が 2 行に折れる（「スタイル付きツ」/「リー」）
  EXPECT_FALSE(has_line(as_written, "スタイル付きツリー"))
      << "この図はもともと折れるはずで、前提が崩れている";
  for (const std::string_view name : kNames) {
    EXPECT_TRUE(has_line(with_nowrap, std::string(name)))
        << name << " が 1 行に収まっていない（nowrap が効いていない）";
  }
  // 折れなくなったので行数は減る
  EXPECT_LT(with_nowrap.size(), as_written.size());
}

// 折り返さないぶん幅を超えることはある。紙面から出れば警告になり、`--strict` で失敗する
// （黙って切れた PNG を返さない。A46）。
TEST(Nowrap, NarrowCanvasStillWarnsAboutOverflow) {
  RenderOptions options = options_for(700);
  const auto lenient = render(pipeline_html(true), japanese_fonts(), options);
  ASSERT_TRUE(lenient.has_value()) << to_string(lenient.error());
  ASSERT_FALSE(lenient->warnings.empty()) << "はみ出しが報告されていない";
  EXPECT_EQ(lenient->warnings[0].kind, WarningKind::ContentOverflow);
  EXPECT_EQ(lenient->warnings[0].overflow_edge, OverflowEdge::Right);
  EXPECT_GT(lenient->warnings[0].overflow_px, 0.0F);

  options.warnings_as_errors = true;
  const auto strict = render(pipeline_html(true), japanese_fonts(), options);
  ASSERT_FALSE(strict.has_value()) << "strict なのに成功した";
  ASSERT_FALSE(strict.error().errors.empty());
  EXPECT_EQ(strict.error().errors[0].kind, ErrorKind::WarningAsError);
}

// 同じ幅（1100）では nowrap でもはみ出さない = 警告なしで使える PNG になる。
TEST(Nowrap, WideEnoughCanvasHasNoWarning) {
  const auto result = render(pipeline_html(true), japanese_fonts(), options_for(1100));
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_TRUE(result->warnings.empty())
      << to_string(result->warnings[0].kind) << " / " << result->warnings[0].detail;
}

// ---- 仕様の要点（end-to-end） -----------------------------------------------------

// `<br>` は nowrap の中でも効く（強制改行は行分割器が no_break_before より優先する）。
TEST(Nowrap, ForcedBreakStillWorksInsideNowrap) {
  constexpr std::string_view kHtml =
      R"(<div style="width:400px;white-space:nowrap">あいうえお<br>かきくけこ</div>)";
  EXPECT_EQ(lines_of(kHtml, 400), (std::vector<std::string>{"あいうえお", "かきくけこ"}));
}

// nowrap の外側の文は今までどおり折れる（span の中だけが 1 かたまりになる）。
TEST(Nowrap, TextOutsideTheNowrapSpanStillWraps) {
  constexpr std::string_view kHtml =
      R"(<div style="width:100px;font-size:16px">ああああ<span style="white-space:nowrap">いいいいい</span>ううううう</div>)";
  const std::vector<std::string> lines = lines_of(kHtml, 200);
  EXPECT_GT(lines.size(), 1U) << "外側の文が折れていない";
  // span の中身は必ずどこか 1 行にまとまって現れる（= 途中で折れていない）
  bool whole = false;
  for (const std::string& line : lines) {
    whole = whole || line.find("いいいいい") != std::string::npos;
  }
  EXPECT_TRUE(whole) << "nowrap の span が折れている";
}

}  // namespace
}  // namespace shashoku::test
