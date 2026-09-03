#include <iostream>
#include <thread>

#include "avox/subtitle/BaseTranslator.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/module/ModuleMgr.hpp"

using namespace avox;

int main() {
  std::cout << "=== Translation Test ===" << std::endl;
  // 走 AvoxManager 工厂表查翻译器(不再直接调 createXxxTranslator —— 符号随组件迁到 plugin dll, 主程序链不到)
  ITranslator* translator = avox::AvoxManager::Get().translatorHub.create("onnx");
  if (!translator) {
    translator = avox::AvoxManager::Get().translatorHub.create("http");
  }
  if (!translator) {
    std::cout << "Failed to create translator (avox_translation plugin 未加载?)" << std::endl;
    return 1;
  }
  // 设置语言对: 日语 -> 中文
  translator->setSourceLanguage(Language::ja);
  translator->setTargetLanguage(Language::zh);

  // 加载
  std::cout << "Loading..." << std::endl;
  if (!translator->open()) {
    std::cout << "Failed to load: " << translator->getLastError() << std::endl;
    delete translator;
    return 1;
  }
  std::cout << "Loaded successfully" << std::endl;

  // 测试日语翻译到中文
  std::cout << "\n=== Test 1: Japanese -> Chinese ===" << std::endl;
  const char* ja_text1 = "こんにちは、元気ですか？";
  const char* result1 = translator->translate(ja_text1);
  std::cout << "Input:  " << ja_text1 << std::endl;
  std::cout << "Output: " << result1 << std::endl;

  // 测试日语翻译到中文 2
  std::cout << "\n=== Test 2: Japanese -> Chinese ===" << std::endl;
  const char* ja_text2 = "今日はいい天気ですね";
  const char* result2 = translator->translate(ja_text2);
  std::cout << "Input:  " << ja_text2 << std::endl;
  std::cout << "Output: " << result2 << std::endl;

  // 测试日语翻译到中文 3
  std::cout << "\n=== Test 3: Japanese -> Chinese ===" << std::endl;
  const char* ja_text3 = "明日の会議の資料を送ってください";
  const char* result3 = translator->translate(ja_text3);
  std::cout << "Input:  " << ja_text3 << std::endl;
  std::cout << "Output: " << result3 << std::endl;

  // 测试长文本（与Python相同）
  std::cout << "\n=== Test 4: Long Text (same as Python) ===" << std::endl;
  const char* ja_text4 =
      "高校生の時、毎週土曜日の午後は友達のリナと一緒に図書館で勉強していました"
      "。リナは数学が得意で、いつも私の分からない問題を丁寧に教えてくれました。"
      "休み時間には、自販機でコーラを買って廊下で話したり、放課後に近くのカフェ"
      "でケーキを食べながら未来の夢について話したりしていました。今でもその頃の"
      "時間がとても懐かしいです。";
  const char* result4 = translator->translate(ja_text4);
  std::cout << "Input:  " << ja_text4 << std::endl;
  std::cout << "Output: " << result4 << std::endl;

  // === 实际语音识别结果测试 ===
  std::cout << "\n=== Test 5-25: Real ASR results ===" << std::endl;

  struct TestCase {
    const char* input;
    const char* reference;  // 当前翻译参考
  };

  TestCase cases[] = {
      // 日常对话/短句
      {"というわけで、例の悪魔に全員やられてしまいました。",
       "所以说，我们全都被那个恶魔打败了。"},
      {"となる？", "变成？"},
      {"うん。", "嗯。"},
      {"取引に応じるだなんて一言も言ってませんよ。",
       "我可没说过要接受交易啊。"},
      {"この私が誰かに脅されて、それに応じるわけがないじゃないですか。",
       "我这样的人被谁威胁，怎么可能答应呢？"},
      {"もう勝手にしなさいよ！", "随便你吧！"},
      {"私たち？", "我们？"},
      {"友達ですよね。", "我们是朋友吧？"},
      {"何があったの？", "发生什么事了？"},
      {"あと、黒猫のぬいぐるみが必要です。買ってきてくださいね。ぬいぐるみ。",
       "还有，需要黑猫玩偶。请买来吧。玩偶。"},
      {"これは姿隠しのスクロールじゃないですか。", "这不是隐身卷轴吗？"},

      // 长句/复杂句
      {"というか、噂のアークプリストが見つからなかった以上、この街であいつに対"
       "抗できるのはもはや私しかいないでしょう。",
       "话说回来，既然找不到传说中的阿克普里斯特，在这个城里能对抗那家伙的就只"
       "有我了吧。"},
      {"何んでも言うことを聞くと思ったら大間違いよ "
       "でも、ちょっとだけ嬉しい自分が悔や！",
       "以为我什么都听你的话就大错特错了，不过有点高兴的自己真让人懊恼！"},
      {"その辺で時間を潰していてくださいと言ったのにだって軍資金を調達してくる"
       "なんて言って宿に入っていったらそりゃ心配でしょう。",
       "明明说了让你们在附近打发时间的，结果说要筹集军费就进了旅馆，那当然会担"
       "心啊。"},
      {"一千万エリスです。一体何をやったらそんな大金を稼げるのよ。",
       "一千万艾莉丝。到底做什么才能赚那么多钱啊。"},
      {"それよりもこれを有効に使って戦いを有利に進めますよ。",
       "与其这样，不如有效利用这个让战斗朝有利的方向发展。"},
      {"ゆんゆは魔道具店に行って使いそうなアイテムを片っ端から買ってきてくださ"
       "い。",
       "悠悠去魔道具店，把可能用得上的道具全都买回来吧。"},
      {"どれもこれもが紅馬の里製ですね お、でかしました。",
       "这些全是红马村出产的呢，哦，干得好。"},
      {"魔族は売られた喧嘩は必ず買うのです、めぐみんだって一応紙一人でバカじゃ"
       "ないんだから、相手との実力差ぐらいはわかるでしょ、言葉にトゲがあります"
       "よ！",
       "魔族被人挑衅打架是一定会应战的，惠美好歹也不傻，对方实力差距还是能看出"
       "来的吧，说话别带刺啊！"},
  };

  int caseCount = sizeof(cases) / sizeof(cases[0]);
  for (int i = 0; i < caseCount; i++) {
    const char* result = translator->translate(cases[i].input);
    std::cout << "\n--- Case " << (i + 5) << " ---" << std::endl;
    std::cout << "Input:     " << cases[i].input << std::endl;
    std::cout << "Output:    " << result << std::endl;
    std::cout << "Reference: " << cases[i].reference << std::endl;
  }

  // 等待用户输入再退出
  std::cout << "\n=== Tests completed, press Enter to exit ===" << std::endl;
  std::cin.get();

  // 释放资源
  translator->close();
  delete translator;

  return 0;
}