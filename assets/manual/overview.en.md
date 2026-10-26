# App guide

## Importing banks

1. Select the Import icon in the top-right corner of the Home screen.
2. Pick a bank file in the system file picker. Text banks can use `.quiz`, `.txt`, or `.text`; Word files can use `.doc`, `.docx`, `.docm`, `.dot`, `.dotx`, or `.dotm`.
3. After a successful import, the bank appears in the Home screen list.

Plain-text banks must use UTF-8 and contain a unique bank ID. Open XML Word files are read from the main document body. A legacy `.doc` file is accepted only when its bank text can be recovered. Android uses the system document picker and reads only files you select.

## Home and practice modes

- Linear: practice in bank order, preferring unanswered or missed questions.
- Shuffle: randomize this round.
- Smart: choose questions from mastery, difficulty, and tags, then add follow-up questions while you answer.
- Recite: show the whole bank in order with answers displayed; the round-size limit does not apply.
- Exam: available only for banks that declare exam papers.

Questions per round applies to Linear, Shuffle, and Smart. Recite and Exam ignore it.

## Answering

- Single choice: select one option and submit.
- Multiple choice: select every correct option.
- True / false: choose True or False.
- Fill in the blank: text updates immediately; submit after all blanks are filled.
- Partial-credit multi-select: all correct selections earn full or proportional credit; selecting a wrong option earns zero.
- Ordered matching: choose one option for each slot.

After submission, the app shows the verdict, score, correct answer, and explanation. Wrong or partially correct questions enter the missed-question list.

## Material questions

A material and all of its subquestions appear on one page. The page scrolls vertically, and subquestions can use any supported question type.

Material pages can submit each question separately or the whole page together. In Exam mode, answers remain editable drafts until submission.

## Exam mode

1. Select Exam on the Home screen.
2. Start the exam.
3. Choose a paper and review its question count, time limit, and pass mark.
4. Answer questions; answers can be changed at any time.
5. Submit and review the score, accuracy, time, and per-question result.

Correctness is not shown during the exam. A timed exam submits automatically when time runs out. Blank answers score zero but do not enter the missed-question list.

## Missed questions, bookmarks, and tags

Wrong or partially correct questions enter the missed-question list. A question can leave automatically after its correct-to-wrong ratio reaches the configured threshold. You can also remove it manually after answering correctly from the missed-only round.

Use the star to bookmark or unbookmark a question. Bank tags and your own tags can be used for filtering and export.

## Results and statistics

The round-complete page shows the number of questions, correct count, accuracy, time, and missed questions.

Statistics shows total answers, accuracy, total practice time, streak, submissions in the last seven days, and completion by bank.

## Export and backup

You can export the whole bank, missed questions, bookmarks, or questions with selected tags. Exported files are standard `.quiz` banks and can be imported again.

Back up important banks, corrected missed questions, and custom-tagged questions regularly.

## Config

- Answering: auto-advance, auto-submit, material-page submission, missed-question move-out ratio, and Smart follow-up interval.
- Interface: language, interface ratio, and Home background.
- Theme: expand a group to edit colors; presets and theme text are also available.
- Prompts: copy the built-in bank-format correction prompts.
- Manual: Overview and Icon guide.

## Gestures and Back

Long lists and pages can be dragged vertically from their content area. Dragging from a button or input also scrolls the page, with inertia after release.

Android Back moves one level at a time. Settings subpages return to the Config root; Home requires two Back actions to exit; leaving an active exam asks for confirmation first.

## Privacy and feedback

The app does not connect to the network or collect accounts, device identifiers, advertising data, analytics, crash reports, or push tokens. Banks, progress, settings, and prompts stay on this device.

Submit feedback through Issues:

- AtomGit: https://atomgit.com/gcw_7wG3NXCK/AxtUebing/issues
- GitHub: https://github.com/mh-zy/AxtUebung/issues

You can open either page from Config or copy its URL to the clipboard.

When reporting a problem, include the version, device model, system version, reproduction steps, actual result, and expected result.
