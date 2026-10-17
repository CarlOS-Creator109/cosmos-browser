#!/usr/bin/env python3
"""Genera wk-i18n.h desde esta tabla. Orden de idiomas: en es ja zh ko ar ru uk.
Edita las traducciones aquí y ejecuta:  python3 gen_i18n.py"""
LANGS = [("en", "English"), ("es", "Español"), ("ja", "日本語"), ("zh", "中文"),
         ("ko", "한국어"), ("ar", "العربية"), ("ru", "Русский"), ("uk", "Українська")]

T = {
"NEW_TAB": ("New tab", "Nueva pestaña", "新しいタブ", "新标签页", "새 탭", "علامة تبويب جديدة", "Новая вкладка", "Нова вкладка"),
"PRIVATE": ("Private", "Privada", "プライベート", "隐私", "비공개", "خاص", "Приватная", "Приватна"),
"LOADING": ("Loading...", "Cargando...", "読み込み中...", "加载中...", "불러오는 중...", "جارٍ التحميل...", "Загрузка...", "Завантаження..."),
"ADDR": ("Search or enter address", "Buscar o escribir una dirección", "検索またはアドレスを入力", "搜索或输入网址", "검색하거나 주소 입력", "ابحث أو اكتب عنواناً", "Поиск или адрес", "Пошук або адреса"),
"BM_ADDED": ("Bookmark saved", "Marcador guardado", "ブックマークを保存しました", "已保存书签", "북마크를 저장했습니다", "تم حفظ الإشارة المرجعية", "Закладка сохранена", "Закладку збережено"),
"BM_REMOVED": ("Bookmark removed", "Marcador quitado", "ブックマークを削除しました", "已删除书签", "북마크를 삭제했습니다", "تمت إزالة الإشارة المرجعية", "Закладка удалена", "Закладку видалено"),
"M_PRIVATE": ("New private tab", "Nueva pestaña privada", "新しいプライベートタブ", "新建隐私标签页", "새 비공개 탭", "علامة تبويب خاصة جديدة", "Новая приватная вкладка", "Нова приватна вкладка"),
"M_HISTORY": ("History", "Historial", "履歴", "历史记录", "방문 기록", "السجل", "История", "Історія"),
"M_BOOKMARKS": ("Bookmarks", "Marcadores", "ブックマーク", "书签", "북마크", "الإشارات المرجعية", "Закладки", "Закладки"),
"M_PASSWORDS": ("Passwords", "Contraseñas", "パスワード", "密码", "비밀번호", "كلمات المرور", "Пароли", "Паролі"),
"M_LOCK": ("Lock passwords", "Bloquear contraseñas", "パスワードをロック", "锁定密码", "비밀번호 잠금", "قفل كلمات المرور", "Заблокировать пароли", "Заблокувати паролі"),
"M_IMPORT": ("Import passwords / cookies…", "Importar contraseñas / cookies…", "パスワード / Cookie をインポート…", "导入密码 / Cookie…", "비밀번호 / 쿠키 가져오기…", "استيراد كلمات المرور / الكوكيز…", "Импорт паролей / cookies…", "Імпорт паролів / cookies…"),
"M_DOWNLOADS": ("Downloads", "Descargas", "ダウンロード", "下载", "다운로드", "التنزيلات", "Загрузки", "Завантаження"),
"M_LANGUAGE": ("Language", "Idioma", "言語", "语言", "언어", "اللغة", "Язык", "Мова"),
"M_H264": ("Force H.264 on YouTube", "Forzar H.264 en YouTube", "YouTube で H.264 を強制", "在 YouTube 上强制使用 H.264", "YouTube에서 H.264 강제", "فرض H.264 على يوتيوب", "Принудительно H.264 на YouTube", "Примусово H.264 на YouTube"),
"M_ADBLOCK": ("Block ads and trackers", "Bloquear anuncios y rastreadores", "広告とトラッカーをブロック", "屏蔽广告和跟踪器", "광고 및 추적기 차단", "حظر الإعلانات والمتتبعات", "Блокировать рекламу и трекеры", "Блокувати рекламу та трекери"),
"M_ABOUT": ("About Cosmos", "Acerca de Cosmos", "Cosmos について", "关于 Cosmos", "Cosmos 정보", "حول Cosmos", "О Cosmos", "Про Cosmos"),
"PW_LOCKED_STATUS": ("Passwords locked", "Contraseñas bloqueadas", "パスワードをロックしました", "密码已锁定", "비밀번호가 잠겼습니다", "تم قفل كلمات المرور", "Пароли заблокированы", "Паролі заблоковано"),
"CANCEL": ("Cancel", "Cancelar", "キャンセル", "取消", "취소", "إلغاء", "Отмена", "Скасувати"),
"OK": ("OK", "Aceptar", "OK", "确定", "확인", "موافق", "ОК", "ОК"),
"MASTER": ("Master password", "Contraseña maestra", "マスターパスワード", "主密码", "마스터 비밀번호", "كلمة المرور الرئيسية", "Мастер-пароль", "Майстер-пароль"),
"MASTER2": ("Repeat the master password", "Repite la contraseña maestra", "マスターパスワードを再入力", "再次输入主密码", "마스터 비밀번호 다시 입력", "أعد كتابة كلمة المرور الرئيسية", "Повторите мастер-пароль", "Повторіть майстер-пароль"),
"MISMATCH": ("The passwords do not match. Try again.", "Las contraseñas no coinciden. Inténtalo otra vez.", "パスワードが一致しません。もう一度お試しください。", "两次输入的密码不一致，请重试。", "비밀번호가 일치하지 않습니다. 다시 시도하세요.", "كلمتا المرور غير متطابقتين. حاول مرة أخرى.", "Пароли не совпадают. Попробуйте ещё раз.", "Паролі не збігаються. Спробуйте ще раз."),
"CREATE_TITLE": ("Create master password", "Crear contraseña maestra", "マスターパスワードの作成", "创建主密码", "마스터 비밀번호 만들기", "إنشاء كلمة المرور الرئيسية", "Создать мастер-пароль", "Створити майстер-пароль"),
"CREATE_MSG": ("Cosmos will store your passwords encrypted with a master password. If you forget it, it CANNOT be recovered. Choose one you will remember:",
               "Cosmos guardará tus contraseñas cifradas con una contraseña maestra. Si la olvidas NO se puede recuperar. Elige una que recuerdes:",
               "Cosmos はパスワードをマスターパスワードで暗号化して保存します。忘れると復元できません。覚えられるものを選んでください：",
               "Cosmos 会用主密码加密保存你的密码。如果忘记主密码，将无法恢复。请设置一个你能记住的密码：",
               "Cosmos는 마스터 비밀번호로 암호화하여 비밀번호를 저장합니다. 잊어버리면 복구할 수 없습니다. 기억할 수 있는 것을 고르세요:",
               "سيحفظ Cosmos كلمات مرورك مشفّرة بكلمة مرور رئيسية. إذا نسيتها فلا يمكن استعادتها. اختر كلمة تتذكرها:",
               "Cosmos сохранит ваши пароли в зашифрованном виде под мастер-паролем. Если вы его забудете, восстановить его НЕЛЬЗЯ. Выберите тот, который запомните:",
               "Cosmos збереже ваші паролі в зашифрованому вигляді під майстер-паролем. Якщо ви його забудете, відновити його НЕМОЖЛИВО. Оберіть той, який запам'ятаєте:"),
"UNLOCK_TITLE": ("Unlock passwords", "Desbloquear contraseñas", "パスワードのロック解除", "解锁密码", "비밀번호 잠금 해제", "فتح قفل كلمات المرور", "Разблокировать пароли", "Розблокувати паролі"),
"UNLOCK_MSG": ("Enter your master password to unlock the saved passwords:", "Escribe tu contraseña maestra para desbloquear las contraseñas guardadas:", "保存したパスワードのロックを解除するには、マスターパスワードを入力してください：", "请输入主密码以解锁已保存的密码：", "저장된 비밀번호의 잠금을 해제하려면 마스터 비밀번호를 입력하세요:", "اكتب كلمة المرور الرئيسية لفتح كلمات المرور المحفوظة:", "Введите мастер-пароль, чтобы разблокировать сохранённые пароли:", "Введіть майстер-пароль, щоб розблокувати збережені паролі:"),
"WRONG_PW": ("Wrong password. Try again:", "Contraseña incorrecta. Inténtalo otra vez:", "パスワードが違います。もう一度入力してください：", "密码错误，请重试：", "비밀번호가 틀렸습니다. 다시 입력하세요:", "كلمة المرور غير صحيحة. حاول مرة أخرى:", "Неверный пароль. Попробуйте ещё раз:", "Невірний пароль. Спробуйте ще раз:"),
"SAVE_Q": ('Save the password for "%1$s" on %2$s?', '¿Guardar la contraseña de "%1$s" para %2$s?', '%2$s の「%1$s」のパスワードを保存しますか？', '要保存 %2$s 上“%1$s”的密码吗？', '%2$s의 "%1$s" 비밀번호를 저장할까요?', 'هل تريد حفظ كلمة مرور "%1$s" لـ %2$s؟', 'Сохранить пароль для «%1$s» на %2$s?', 'Зберегти пароль для «%1$s» на %2$s?'),
"NO_USER": ("(no username)", "(sin usuario)", "(ユーザー名なし)", "(无用户名)", "(사용자 이름 없음)", "(بدون اسم مستخدم)", "(без имени)", "(без імені)"),
"NOT_NOW": ("Not now", "Ahora no", "今はしない", "暂不", "나중에", "ليس الآن", "Не сейчас", "Не зараз"),
"UPDATE": ("Update", "Actualizar", "更新", "更新", "업데이트", "تحديث", "Обновить", "Оновити"),
"SAVE": ("Save", "Guardar", "保存", "保存", "저장", "حفظ", "Сохранить", "Зберегти"),
"IMP_TITLE": ("Import from another browser: passwords CSV, cookies.txt or Firefox cookies.sqlite", "Importar de otro navegador: CSV de contraseñas, cookies.txt o cookies.sqlite de Firefox", "他のブラウザーからインポート：パスワードの CSV、cookies.txt、または Firefox の cookies.sqlite", "从其他浏览器导入：密码 CSV、cookies.txt 或 Firefox 的 cookies.sqlite", "다른 브라우저에서 가져오기: 비밀번호 CSV, cookies.txt 또는 Firefox cookies.sqlite", "استيراد من متصفح آخر: ملف CSV لكلمات المرور أو cookies.txt أو cookies.sqlite الخاص بفايرفوكس", "Импорт из другого браузера: CSV с паролями, cookies.txt или cookies.sqlite Firefox", "Імпорт з іншого браузера: CSV із паролями, cookies.txt або cookies.sqlite Firefox"),
"IMPORT": ("Import", "Importar", "インポート", "导入", "가져오기", "استيراد", "Импорт", "Імпорт"),
"IMP_FF_FAIL": ("Could not read that file. For Firefox, use the cookies.sqlite from your profile (not Chrome's: it is encrypted).",
                "No pude leer ese archivo. Para Firefox usa el cookies.sqlite de tu perfil (no el de Chrome: ese va cifrado).",
                "ファイルを読み込めませんでした。Firefox の場合はプロファイル内の cookies.sqlite を使ってください（Chrome のものは暗号化されています）。",
                "无法读取该文件。Firefox 请使用配置文件中的 cookies.sqlite（Chrome 的已加密，不可用）。",
                "파일을 읽을 수 없습니다. Firefox는 프로필의 cookies.sqlite를 사용하세요(Chrome 것은 암호화되어 있습니다).",
                "تعذّرت قراءة الملف. في فايرفوكس استخدم cookies.sqlite من ملفك الشخصي (ملف Chrome مشفّر).",
                "Не удалось прочитать файл. Для Firefox используйте cookies.sqlite из профиля (у Chrome он зашифрован).",
                "Не вдалося прочитати файл. Для Firefox використайте cookies.sqlite із профілю (у Chrome він зашифрований)."),
"IMP_COOK_OK": ("Cookies imported: %d.", "Cookies importadas: %d.", "Cookie をインポートしました：%d 件", "已导入 Cookie：%d 个", "쿠키를 가져왔습니다: %d개", "تم استيراد الكوكيز: %d", "Cookies импортировано: %d.", "Cookies імпортовано: %d."),
"IMP_NEED_MASTER": ("Cancelled: the master password is needed to store the passwords.", "Cancelado: hace falta la contraseña maestra para guardar las contraseñas.", "キャンセルしました：パスワードの保存にはマスターパスワードが必要です。", "已取消：保存密码需要主密码。", "취소됨: 비밀번호를 저장하려면 마스터 비밀번호가 필요합니다.", "أُلغي: كلمة المرور الرئيسية مطلوبة لحفظ كلمات المرور.", "Отменено: для сохранения паролей нужен мастер-пароль.", "Скасовано: для збереження паролів потрібен майстер-пароль."),
"IMP_CSV_BAD": ("That CSV has no url, username and password columns (is it a passwords export?).", "Ese CSV no tiene columnas url, username y password (¿es de contraseñas?).", "この CSV には url・username・password 列がありません（パスワードのエクスポートですか？）", "该 CSV 没有 url、username、password 列（确定是密码导出文件吗？）", "이 CSV에는 url, username, password 열이 없습니다(비밀번호 내보내기 파일인가요?).", "لا يحتوي ملف CSV هذا على الأعمدة url وusername وpassword (هل هو ملف تصدير كلمات المرور؟).", "В этом CSV нет столбцов url, username и password (это экспорт паролей?).", "У цьому CSV немає стовпців url, username і password (це експорт паролів?)."),
"IMP_READ_FAIL": ("Could not read that file.", "No pude leer ese archivo.", "ファイルを読み込めませんでした。", "无法读取该文件。", "파일을 읽을 수 없습니다.", "تعذّرت قراءة الملف.", "Не удалось прочитать файл.", "Не вдалося прочитати файл."),
"IMP_PW_OK": ("Passwords imported: %1$d (skipped: %2$d, no http/https address).\n\nWARNING: that CSV holds your passwords UNENCRYPTED. Delete it now (and empty the trash).",
              "Contraseñas importadas: %1$d (ignoradas: %2$d, sin dirección http/https).\n\nOJO: ese CSV tiene tus contraseñas SIN cifrar. Bórralo ya (y vacía la papelera).",
              "パスワードをインポートしました：%1$d 件（スキップ：%2$d 件、http/https アドレスなし）。\n\n注意：この CSV にはパスワードが暗号化されずに入っています。今すぐ削除してください（ごみ箱も空にしてください）。",
              "已导入密码：%1$d 个（跳过：%2$d 个，无 http/https 地址）。\n\n注意：该 CSV 中的密码是未加密的。请立即删除它（并清空回收站）。",
              "비밀번호를 가져왔습니다: %1$d개(건너뜀: %2$d개, http/https 주소 없음).\n\n주의: 이 CSV에는 비밀번호가 암호화되지 않은 채 들어 있습니다. 지금 삭제하세요(휴지통도 비우세요).",
              "تم استيراد كلمات المرور: %1$d (تم تخطي: %2$d، بدون عنوان http/https).\n\nتنبيه: ملف CSV هذا يحتوي كلمات مرورك دون تشفير. احذفه الآن (وأفرغ سلة المحذوفات).",
              "Паролей импортировано: %1$d (пропущено: %2$d, нет адреса http/https).\n\nВНИМАНИЕ: этот CSV хранит ваши пароли БЕЗ шифрования. Удалите его сейчас (и очистите корзину).",
              "Паролів імпортовано: %1$d (пропущено: %2$d, немає адреси http/https).\n\nУВАГА: цей CSV зберігає ваші паролі БЕЗ шифрування. Видаліть його зараз (і очистіть кошик)."),
"IMP_TXT_NONE": ("No cookies found in that file (Netscape cookies.txt format expected).", "No encontré cookies en ese archivo (se espera formato cookies.txt de Netscape).", "このファイルに Cookie が見つかりません（Netscape 形式の cookies.txt が必要です）。", "该文件中没有 Cookie（需要 Netscape 格式的 cookies.txt）。", "이 파일에서 쿠키를 찾을 수 없습니다(Netscape cookies.txt 형식 필요).", "لم أجد كوكيز في الملف (المتوقع تنسيق cookies.txt من Netscape).", "В файле нет cookies (ожидается формат Netscape cookies.txt).", "У файлі немає cookies (очікується формат Netscape cookies.txt)."),
"DL_TITLE": ("Downloads", "Descargas", "ダウンロード", "下载", "다운로드", "التنزيلات", "Загрузки", "Завантаження"),
"DL_EMPTY": ("No downloads yet", "Aún no hay descargas", "ダウンロードはまだありません", "还没有下载", "아직 다운로드가 없습니다", "لا توجد تنزيلات بعد", "Загрузок пока нет", "Завантажень ще немає"),
"DL_DONE": ("Done", "Completada", "完了", "已完成", "완료", "اكتمل", "Готово", "Готово"),
"DL_FAILED": ("Failed", "Falló", "失敗", "失败", "실패", "فشل", "Ошибка", "Помилка"),
"DL_CANCELED": ("Cancelled", "Cancelada", "キャンセル済み", "已取消", "취소됨", "أُلغي", "Отменено", "Скасовано"),
"DL_PROGRESS": ("Downloading… %d%%", "Descargando… %d%%", "ダウンロード中… %d%%", "正在下载… %d%%", "다운로드 중… %d%%", "جارٍ التنزيل… %d%%", "Загрузка… %d%%", "Завантаження… %d%%"),
"DL_OPEN_FOLDER": ("Open downloads folder", "Abrir carpeta de descargas", "ダウンロードフォルダーを開く", "打开下载文件夹", "다운로드 폴더 열기", "فتح مجلد التنزيلات", "Открыть папку загрузок", "Відкрити теку завантажень"),
"DL_CLEAR": ("Clear list", "Limpiar lista", "リストを消去", "清空列表", "목록 지우기", "مسح القائمة", "Очистить список", "Очистити список"),
"DL_STATUS": ("Downloading…", "Descargando…", "ダウンロード中…", "正在下载…", "다운로드 중…", "جارٍ التنزيل…", "Загрузка…", "Завантаження…"),
"DL_FINISHED": ("Download finished: %s", "Descarga terminada: %s", "ダウンロード完了：%s", "下载完成：%s", "다운로드 완료: %s", "اكتمل التنزيل: %s", "Загрузка завершена: %s", "Завантаження завершено: %s"),
"DL_MISSING": ("File moved or deleted", "El archivo se movió o se borró", "ファイルが移動または削除されました", "文件已被移动或删除", "파일이 이동되었거나 삭제되었습니다", "تم نقل الملف أو حذفه", "Файл перемещён или удалён", "Файл переміщено або видалено"),
"T_HOME": ("Home", "Inicio", "ホーム", "主页", "홈", "الرئيسية", "Главная", "Головна"),
"T_ABOUT": ("About Cosmos", "Acerca de Cosmos", "Cosmos について", "关于 Cosmos", "Cosmos 정보", "حول Cosmos", "О Cosmos", "Про Cosmos"),
"HOME_SUB": ("Made by COS-Mods · WebKit engine", "Hecho por COS-Mods · motor WebKit", "COS-Mods 製 · WebKit エンジン", "COS-Mods 出品 · WebKit 引擎", "COS-Mods 제작 · WebKit 엔진", "من صنع COS-Mods · محرك WebKit", "Сделано COS-Mods · движок WebKit", "Створено COS-Mods · рушій WebKit"),
"HOME_SEARCH": ("Search DuckDuckGo or type an address", "Buscar en DuckDuckGo o escribir una dirección", "DuckDuckGo で検索、またはアドレスを入力", "在 DuckDuckGo 搜索或输入网址", "DuckDuckGo 검색 또는 주소 입력", "ابحث في DuckDuckGo أو اكتب عنواناً", "Поиск в DuckDuckGo или адрес", "Пошук у DuckDuckGo або адреса"),
"HIST_SEARCH": ("Search history", "Buscar en el historial", "履歴を検索", "搜索历史记录", "방문 기록 검색", "ابحث في السجل", "Поиск по истории", "Пошук в історії"),
"HIST_CLEAR": ("Clear all history", "Borrar todo el historial", "履歴をすべて削除", "清除全部历史记录", "방문 기록 모두 삭제", "مسح كل السجل", "Очистить всю историю", "Очистити всю історію"),
"HIST_EMPTY": ("Nothing here.", "Nada por aquí.", "ここには何もありません。", "这里什么也没有。", "여기에는 아무것도 없습니다.", "لا شيء هنا.", "Здесь пусто.", "Тут порожньо."),
"BM_EMPTY": ("No bookmarks yet. Use Ctrl+D or the star.", "Todavía no hay marcadores. Usa Ctrl+D o la estrella.", "ブックマークはまだありません。Ctrl+D か星印を使ってください。", "还没有书签。请使用 Ctrl+D 或星标。", "아직 북마크가 없습니다. Ctrl+D 또는 별 버튼을 사용하세요.", "لا توجد إشارات مرجعية بعد. استخدم Ctrl+D أو النجمة.", "Закладок пока нет. Используйте Ctrl+D или звёздочку.", "Закладок ще немає. Скористайтеся Ctrl+D або зірочкою."),
"REMOVE": ("Remove", "Quitar", "削除", "移除", "삭제", "إزالة", "Убрать", "Прибрати"),
"PW_LOCKED_MSG": ("The vault is locked. Press Ctrl+Shift+P to unlock it (or open this page from the menu).", "La bóveda está bloqueada. Pulsa Ctrl+Shift+P para desbloquearla (o abre esta página desde el menú).", "保管庫はロックされています。Ctrl+Shift+P で解除してください（またはメニューからこのページを開いてください）。", "密码库已锁定。按 Ctrl+Shift+P 解锁（或从菜单打开此页面）。", "보관함이 잠겨 있습니다. Ctrl+Shift+P로 잠금을 해제하세요(또는 메뉴에서 이 페이지를 여세요).", "الخزنة مقفلة. اضغط Ctrl+Shift+P لفتحها (أو افتح هذه الصفحة من القائمة).", "Хранилище заблокировано. Нажмите Ctrl+Shift+P, чтобы разблокировать (или откройте эту страницу из меню).", "Сховище заблоковано. Натисніть Ctrl+Shift+P, щоб розблокувати (або відкрийте цю сторінку з меню)."),
"DELETE": ("Delete", "Borrar", "削除", "删除", "삭제", "حذف", "Удалить", "Видалити"),
"PW_EMPTY": ("No saved passwords.", "No hay contraseñas guardadas.", "保存されたパスワードはありません。", "没有已保存的密码。", "저장된 비밀번호가 없습니다.", "لا توجد كلمات مرور محفوظة.", "Сохранённых паролей нет.", "Збережених паролів немає."),
"PW_NOTE": ("Encrypted with AES-256 and your master password. Passwords are never shown here.", "Cifradas con AES-256 y tu contraseña maestra. Las contraseñas nunca se muestran aquí.", "AES-256 とマスターパスワードで暗号化されています。パスワード自体はここには表示されません。", "使用 AES-256 和你的主密码加密。此处不会显示密码本身。", "AES-256과 마스터 비밀번호로 암호화됩니다. 비밀번호는 여기에 표시되지 않습니다.", "مشفّرة بـ AES-256 وكلمة المرور الرئيسية. لا تُعرض كلمات المرور هنا أبداً.", "Зашифровано AES-256 и вашим мастер-паролем. Сами пароли здесь не показываются.", "Зашифровано AES-256 і вашим майстер-паролем. Самі паролі тут не показуються."),
"ABOUT_ENGINE": ("Own interface (COS-Mods) on the WebKitGTK %u.%u.%u engine.", "Interfaz propia (COS-Mods) sobre el motor WebKitGTK %u.%u.%u.", "独自のインターフェース（COS-Mods）＋ WebKitGTK %u.%u.%u エンジン。", "COS-Mods 自制界面，基于 WebKitGTK %u.%u.%u 引擎。", "자체 인터페이스(COS-Mods) + WebKitGTK %u.%u.%u 엔진.", "واجهة خاصة (COS-Mods) فوق محرك WebKitGTK %u.%u.%u.", "Собственный интерфейс (COS-Mods) на движке WebKitGTK %u.%u.%u.", "Власний інтерфейс (COS-Mods) на рушії WebKitGTK %u.%u.%u."),
"SHORTCUTS": ("Shortcuts: Ctrl+T, Ctrl+W, Ctrl+L, Ctrl+D (bookmark), Ctrl+H (history), Ctrl+J (downloads), Ctrl+Shift+N (private), Ctrl+Shift+P (passwords), F5, F11, F12",
              "Atajos: Ctrl+T, Ctrl+W, Ctrl+L, Ctrl+D (marcador), Ctrl+H (historial), Ctrl+J (descargas), Ctrl+Shift+N (privada), Ctrl+Shift+P (contraseñas), F5, F11, F12",
              "ショートカット：Ctrl+T, Ctrl+W, Ctrl+L, Ctrl+D（ブックマーク）, Ctrl+H（履歴）, Ctrl+J（ダウンロード）, Ctrl+Shift+N（プライベート）, Ctrl+Shift+P（パスワード）, F5, F11, F12",
              "快捷键：Ctrl+T、Ctrl+W、Ctrl+L、Ctrl+D（书签）、Ctrl+H（历史）、Ctrl+J（下载）、Ctrl+Shift+N（隐私）、Ctrl+Shift+P（密码）、F5、F11、F12",
              "단축키: Ctrl+T, Ctrl+W, Ctrl+L, Ctrl+D(북마크), Ctrl+H(방문 기록), Ctrl+J(다운로드), Ctrl+Shift+N(비공개), Ctrl+Shift+P(비밀번호), F5, F11, F12",
              "الاختصارات: Ctrl+T، Ctrl+W، Ctrl+L، Ctrl+D (إشارة مرجعية)، Ctrl+H (السجل)، Ctrl+J (التنزيلات)، Ctrl+Shift+N (خاص)، Ctrl+Shift+P (كلمات المرور)، F5، F11، F12",
              "Горячие клавиши: Ctrl+T, Ctrl+W, Ctrl+L, Ctrl+D (закладка), Ctrl+H (история), Ctrl+J (загрузки), Ctrl+Shift+N (приватная), Ctrl+Shift+P (пароли), F5, F11, F12",
              "Гарячі клавіші: Ctrl+T, Ctrl+W, Ctrl+L, Ctrl+D (закладка), Ctrl+H (історія), Ctrl+J (завантаження), Ctrl+Shift+N (приватна), Ctrl+Shift+P (паролі), F5, F11, F12"),
}


def cstr(s):
    out = []
    for ch in s:
        o = ord(ch)
        if ch == '"': out.append('\\"')
        elif ch == '\\': out.append('\\\\')
        elif ch == '\n': out.append('\\n')
        elif o < 128: out.append(ch)
        else: out.append("".join("\\%03o" % b for b in ch.encode("utf-8")))
    return '"' + "".join(out) + '"'


keys = list(T)
for k, v in T.items():
    assert len(v) == len(LANGS), k
    # los formatos deben coincidir entre idiomas
    import re
    f = lambda s: sorted(re.findall(r"%(?:\d\$)?[a-z%]", s.replace("%%", "")))
    for i, s in enumerate(v[1:], 1):
        assert f(s) == f(v[0]), (k, LANGS[i][0], f(s), f(v[0]))

with open("wk-i18n.h", "w") as o:
    o.write("/* GENERADO por gen_i18n.py - no editar a mano */\n#ifndef WK_I18N_H\n#define WK_I18N_H\n")
    o.write("enum { " + ", ".join("K_" + k for k in keys) + ", K__N };\n")
    o.write("#define LANG_N %d\n" % len(LANGS))
    o.write("static const char *LANG_CODE[LANG_N] = {" + ",".join('"%s"' % c for c, _ in LANGS) + "};\n")
    o.write("static const char *LANG_NAME[LANG_N] = {" + ",".join(cstr(n) for _, n in LANGS) + "};\n")
    o.write("static const char *TR[LANG_N][K__N] = {\n")
    for i in range(len(LANGS)):
        o.write(" {\n")
        for k in keys:
            o.write("  [K_%s] = %s,\n" % (k, cstr(T[k][i])))
        o.write(" },\n")
    o.write("};\n#endif\n")
print("ok", len(keys), "claves,", len(LANGS), "idiomas")
