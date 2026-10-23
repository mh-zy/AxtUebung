# core：平台无关的业务逻辑（可在电脑上直接跑单元测试）
add_library(quiz_core STATIC
    src/app/AppContext.cpp
    src/app/Settings.cpp
    src/app/Stats.cpp
    src/app/ThemePreset.cpp
    src/i18n/I18n.cpp
    src/io/BankFormat.cpp
    src/io/DocumentText.cpp
    src/io/ExamBuilder.cpp
    src/io/TextDocument.cpp
    src/model/Progress.cpp
    src/model/Question.cpp
    src/model/QuestionBank.cpp
    src/model/Scoring.cpp
    src/progress/ProgressStore.cpp
    src/session/QuizSession.cpp
    src/text/MathText.cpp
    src/util/Log.cpp
)

target_include_directories(quiz_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(quiz_core PUBLIC cxx_std_17)
target_link_libraries(quiz_core PRIVATE stb_image)
