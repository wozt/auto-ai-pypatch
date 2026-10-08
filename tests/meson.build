matcher_test = executable('matcher_test',
  files('matcher_test.c', '../src/matcher.c', '../src/config.c', '../src/prompt.c'),
  include_directories: include_directories('../src'),
  dependencies: [dependency('glib-2.0'), gio],
)
test('matcher-and-prompts', matcher_test)

candidate_test = executable('candidate_test',
  files('candidate_test.c', '../src/candidate.c', '../src/history.c',
        '../src/matcher.c', '../src/config.c'),
  include_directories: include_directories('../src'),
  dependencies: [dependency('glib-2.0'), gio],
)
test('candidate-selection-and-history', candidate_test)

executor_test = executable('executor_test',
  files('executor_test.c', '../src/executor.c', '../src/patch_meta.c', '../src/history.c', '../src/config.c'),
  include_directories: include_directories('../src'),
  dependencies: [dependency('glib-2.0'), gio],
)
test('executor-and-applied-history', executor_test, timeout: 30)

git_workflow_test = executable('git_workflow_test',
  files('git_workflow_test.c', '../src/git.c', '../src/patch_meta.c'),
  include_directories: include_directories('../src'),
  dependencies: [dependency('glib-2.0'), gio],
)
test('git-workflow-and-metadata', git_workflow_test, timeout: 40)
