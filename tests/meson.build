matcher_test = executable('matcher_test',
  files('matcher_test.c', '../src/matcher.c', '../src/config.c', '../src/prompt.c'),
  include_directories: include_directories('../src'),
  dependencies: [dependency('glib-2.0'), gio],
)
test('matcher-and-prompts', matcher_test)
