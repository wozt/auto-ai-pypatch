#include "prompt.h"
#include "matcher.h"

typedef struct {
    const char *code;
    const char *name;
    const char *intro;
    const char *instructions;
    const char *metadata;
    const char *filename;
    const char *additional;
} PromptLanguage;

/* The metadata markers are intentionally not translated: they are machine-readable. */
static const PromptLanguage languages[] = {
    {"fr", "Français", "Tu travailles sur le projet",
     "Fournis un fichier Python 3 autonome et téléchargeable, à lancer depuis la racine du projet. Vérifie les préconditions, rends les modifications idempotentes, échoue avec un code non nul si nécessaire, ne touche jamais aux fichiers hors du projet et ne lance aucune commande Git.",
     "Place exactement ces métadonnées en commentaires au début du script :", "Nom de fichier : respecte cette regex", "Autre regex obligatoire (ET)"},
    {"en", "English", "You are working on project",
     "Provide a downloadable standalone Python 3 patch to run from the project root. Check preconditions, make edits idempotent, exit nonzero on failure, never touch files outside the project, and do not run Git commands.",
     "Place these exact metadata comments at the beginning of the script:", "Filename: match this regex", "Additional mandatory regex (AND)"},
    {"de", "Deutsch", "Du arbeitest am Projekt",
     "Erstelle einen eigenständigen, herunterladbaren Python-3-Patch, der im Projektverzeichnis ausgeführt wird. Prüfe die Voraussetzungen, arbeite idempotent, gib bei Fehlern einen Exitcode ungleich null zurück, ändere nichts außerhalb des Projekts und führe keine Git-Befehle aus.",
     "Setze diese Metadaten-Kommentare an den Dateianfang:", "Dateiname: Dieser Regex entsprechen", "Zusätzlicher zwingender Regex (UND)"},
    {"it", "Italiano", "Stai lavorando al progetto",
     "Fornisci uno script di patch Python 3 autonomo e scaricabile, eseguito dalla radice del progetto. Verifica le precondizioni, rendi le modifiche idempotenti, termina con codice diverso da zero in caso di errore, non modificare file esterni al progetto e non eseguire comandi Git.",
     "Metti questi commenti di metadati all'inizio dello script:", "Nome file: rispetta questa regex", "Regex aggiuntiva obbligatoria (AND)"},
    {"es", "Español", "Estás trabajando en el proyecto",
     "Entrega un script de parche Python 3 independiente y descargable para ejecutar desde la raíz del proyecto. Comprueba las condiciones previas, haz que los cambios sean idempotentes, devuelve un código distinto de cero ante errores, no modifiques archivos fuera del proyecto y no ejecutes comandos Git.",
     "Incluye estos comentarios de metadatos al comienzo del script:", "Nombre de archivo: debe coincidir con esta regex", "Regex adicional obligatoria (Y)"},
    {"ja", "日本語", "作業対象のプロジェクト",
     "プロジェクトのルートで実行できる、ダウンロード可能な独立した Python 3 パッチを作成してください。前提条件を確認し、変更を冪等にし、失敗時はゼロ以外の終了コードを返してください。プロジェクト外のファイルを変更せず、Git コマンドも実行しないでください。",
     "スクリプト冒頭に次のメタデータコメントを記載してください：", "ファイル名は次の正規表現に一致させてください", "追加の必須正規表現（AND）"},
    {"zh", "简体中文", "你正在处理项目",
     "请提供可下载、独立运行的 Python 3 补丁脚本，并从项目根目录执行。检查前置条件，确保修改具有幂等性；失败时以非零状态退出。不要修改项目以外的文件，也不要执行 Git 命令。",
     "请在脚本开头写入以下元数据注释：", "文件名必须匹配此正则表达式", "额外必须匹配的正则表达式（与）"},
    {"ko", "한국어", "작업 중인 프로젝트",
     "프로젝트 루트에서 실행할 수 있는 독립적인 Python 3 패치 파일을 다운로드 가능하게 제공하세요. 사전 조건을 검사하고 변경 작업을 멱등적으로 만들며 실패 시 0이 아닌 종료 코드를 반환하세요. 프로젝트 외부 파일을 수정하거나 Git 명령을 실행하지 마세요.",
     "스크립트 시작 부분에 다음 메타데이터 주석을 넣으세요:", "파일 이름은 다음 정규식과 일치해야 합니다", "추가 필수 정규식(AND)"},
    {"ru", "Русский", "Ты работаешь над проектом",
     "Предоставь автономный загружаемый Python 3-скрипт патча, запускаемый из корня проекта. Проверь предварительные условия, обеспечь идемпотентность изменений, возвращай ненулевой код при ошибках. Не меняй файлы вне проекта и не запускай команды Git.",
     "Помести в начало скрипта эти комментарии с метаданными:", "Имя файла должно соответствовать регулярному выражению", "Дополнительное обязательное выражение (И)"},
    {"vi", "Tiếng Việt", "Bạn đang làm việc với dự án",
     "Hãy cung cấp tập lệnh vá Python 3 độc lập có thể tải xuống, chạy từ thư mục gốc của dự án. Kiểm tra điều kiện tiên quyết, bảo đảm thay đổi có tính lặp an toàn, trả mã thoát khác 0 khi lỗi. Không sửa tệp ngoài dự án và không chạy lệnh Git.",
     "Đặt các chú thích siêu dữ liệu sau ở đầu tập lệnh:", "Tên tệp phải khớp biểu thức chính quy", "Biểu thức chính quy bắt buộc bổ sung (AND)"},
    {"tl", "Filipino / Tagalog", "Gumagawa ka sa proyektong",
     "Magbigay ng mada-download at standalone na Python 3 patch script na tatakbo mula sa root ng proyekto. Suriin ang mga prerequisite, gawing idempotent ang mga pagbabago, mag-exit nang hindi zero kapag may error, huwag baguhin ang mga file sa labas ng proyekto, at huwag magpatakbo ng Git commands.",
     "Ilagay ang mga metadata comment na ito sa simula ng script:", "Dapat tumugma ang filename sa regex na ito", "Karagdagang kailangang regex (AND)"},
    {"pt", "Português", "Estás a trabalhar no projeto",
     "Fornece um script de patch Python 3 autónomo e descarregável, executado a partir da raiz do projeto. Verifica as pré-condições, torna as alterações idempotentes, devolve um código não nulo em caso de falha. Não alteres ficheiros fora do projeto nem executes comandos Git.",
     "Coloca estes comentários de metadados no início do script:", "Nome do ficheiro: corresponde a esta regex", "Regex adicional obrigatória (E)"},
    {"ar", "العربية", "أنت تعمل على المشروع",
     "قدّم ملف تصحيح Python 3 مستقلاً وقابلاً للتنزيل، يُشغَّل من جذر المشروع. تحقّق من الشروط المسبقة واجعل التعديلات قابلة للتكرار بأمان وأعد رمز خروج غير صفري عند الفشل. لا تعدّل ملفات خارج المشروع ولا تُشغّل أوامر Git.",
     "ضع تعليقات البيانات الوصفية التالية في بداية الملف:", "يجب أن يطابق اسم الملف التعبير النمطي", "تعبير نمطي إضافي إلزامي (AND)"},
    {"hi", "हिन्दी", "आप इस प्रोजेक्ट पर काम कर रहे हैं",
     "प्रोजेक्ट की मूल डायरेक्टरी से चलने वाली, डाउनलोड की जा सकने वाली स्वतंत्र Python 3 पैच स्क्रिप्ट दें। पहले आवश्यक शर्तें जाँचें, बदलावों को दोबारा चलाने पर सुरक्षित रखें, त्रुटि पर गैर-शून्य एग्ज़िट कोड दें। प्रोजेक्ट के बाहर फ़ाइलें न बदलें और Git कमांड न चलाएँ।",
     "स्क्रिप्ट की शुरुआत में ये मेटाडेटा टिप्पणियाँ रखें:", "फ़ाइल नाम इस रेगुलर एक्सप्रेशन से मेल खाए", "अतिरिक्त अनिवार्य रेगुलर एक्सप्रेशन (AND)"},
    {"id", "Bahasa Indonesia", "Anda sedang mengerjakan proyek",
     "Berikan skrip patch Python 3 mandiri yang dapat diunduh dan dijalankan dari direktori utama proyek. Periksa prasyarat, buat perubahan idempoten, keluar dengan kode bukan nol jika gagal, jangan ubah berkas di luar proyek, dan jangan jalankan perintah Git.",
     "Letakkan komentar metadata ini di awal skrip:", "Nama berkas harus cocok dengan regex", "Regex tambahan yang wajib cocok (DAN)"},
    {"tr", "Türkçe", "Üzerinde çalıştığın proje",
     "Proje kökünden çalıştırılabilecek, indirilebilir bağımsız bir Python 3 yama betiği sağla. Ön koşulları denetle, değişiklikleri yinelenebilir ve güvenli yap, hata durumunda sıfır olmayan çıkış kodu döndür. Proje dışındaki dosyalara dokunma ve Git komutu çalıştırma.",
     "Betiğin başına şu üst veri yorumlarını ekle:", "Dosya adı şu düzenli ifadeyle eşleşmeli", "Ek zorunlu düzenli ifade (VE)"},
    {"nl", "Nederlands", "Je werkt aan project",
     "Lever een downloadbaar, zelfstandig Python 3-patchscript dat vanuit de projectmap wordt uitgevoerd. Controleer de voorwaarden, maak wijzigingen idempotent, gebruik bij fouten een niet-nul exitcode, wijzig geen bestanden buiten het project en voer geen Git-commando's uit.",
     "Plaats deze metadata-commentaren bovenaan het script:", "Bestandsnaam: voldoe aan deze regex", "Extra verplichte regex (EN)"},
    {"pl", "Polski", "Pracujesz nad projektem",
     "Przygotuj samodzielny skrypt poprawki Python 3 do pobrania, uruchamiany z katalogu głównego projektu. Sprawdź warunki wstępne, zapewnij idempotentność zmian, przy błędzie zwróć kod różny od zera. Nie modyfikuj plików poza projektem ani nie uruchamiaj poleceń Git.",
     "Umieść na początku skryptu następujące komentarze z metadanymi:", "Nazwa pliku musi pasować do regex", "Dodatkowe obowiązkowe wyrażenie (I)"},
    {"uk", "Українська", "Ти працюєш над проєктом",
     "Надай автономний Python 3-скрипт патча для завантаження, який запускається з кореня проєкту. Перевір попередні умови, зроби зміни ідемпотентними, повертай ненульовий код у разі помилки. Не змінюй файли поза проєктом і не запускай команди Git.",
     "Додай на початок скрипта такі коментарі з метаданими:", "Назва файлу має відповідати регулярному виразу", "Додатковий обов’язковий вираз (І)"},
    {"th", "ไทย", "คุณกำลังทำงานกับโปรเจกต์",
     "จัดเตรียมสคริปต์แพตช์ Python 3 แบบแยกเดี่ยวที่ดาวน์โหลดได้และรันจากโฟลเดอร์หลักของโปรเจกต์ ตรวจสอบเงื่อนไขเบื้องต้น ทำให้การแก้ไขรันซ้ำได้อย่างปลอดภัย คืนรหัสที่ไม่ใช่ศูนย์เมื่อเกิดข้อผิดพลาด อย่าแก้ไขไฟล์นอกโปรเจกต์หรือเรียกคำสั่ง Git",
     "ใส่คอมเมนต์ข้อมูลกำกับต่อไปนี้ไว้ต้นสคริปต์:", "ชื่อไฟล์ต้องตรงกับ regex นี้", "regex เพิ่มเติมที่ต้องตรงด้วย (AND)"},
    {"sv", "Svenska", "Du arbetar med projektet",
     "Skapa ett nedladdningsbart, fristående Python 3-patchskript som körs från projektets rot. Kontrollera förutsättningarna, gör ändringarna idempotenta, returnera en felkod som inte är noll vid misslyckande. Ändra inte filer utanför projektet och kör inga Git-kommandon.",
     "Placera följande metadatakommentarer i början av skriptet:", "Filnamnet ska matcha detta regex", "Ytterligare obligatoriskt regex (OCH)"},
};

guint aa_prompt_language_count(void) {
    return G_N_ELEMENTS(languages);
}
const char *aa_prompt_language_name(guint i) {
    return i < aa_prompt_language_count() ? languages[i].name : languages[0].name;
}
const char *aa_prompt_language_code(guint i) {
    return i < aa_prompt_language_count() ? languages[i].code : languages[0].code;
}
guint aa_prompt_language_index(const char *code) {
    for (guint i = 0; i < aa_prompt_language_count(); i++)
        if (g_strcmp0(code, languages[i].code) == 0) return i;
    return 0;
}

char *aa_prompt_generate(const AaConfig *c) {
    const PromptLanguage *lang = &languages[aa_prompt_language_index(c->language)];
    /* Never suggest a fake project identifier when no project is selected. */
    if (!c->project_dir || !*c->project_dir)
        return g_strdup("Sélectionne d'abord le dossier projet pour générer le prompt. / Select a project directory first.");
    g_autofree char *project = g_path_get_basename(c->project_dir);
    g_autoptr(GError) error = NULL;
    g_autofree char *expected = aa_matcher_preview(c, &error);
    if (!expected) expected = g_strdup("<invalid regex>");
    return g_strdup_printf(
        "%s \"%s\".\n\n%s\n\n%s\n"
        "# Auto-AI-PyPatch: commit-message: feat: implement requested changes\n"
        "# Auto-AI-PyPatch: project: %s\n\n"
        "Copy these two lines verbatim to the opening Python comments; keep the exact project identifier "
        "and replace only the commit-message description with a specific summary. "
        "Do not add angle brackets, and never substitute the literal word 'project'.\n\n"
        "%s: `%s`\n%s: `%s`\n",
        lang->intro, project, lang->instructions, lang->metadata, project,
        lang->filename, expected, lang->additional,
        c->regex && *c->regex ? c->regex : ".*");
}
