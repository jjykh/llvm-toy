{
  'includes': [
    'common.gypi',
  ],
  'targets': [
    {
      'target_name': 'All',
      'type': 'none',
      'dependencies': [
          '<(DEPTH)/main.gyp:*',
          '<(DEPTH)/tf/tests/tests.gyp:*',
          '<(DEPTH)/llvm/tests/tests.gyp:*',
      ]
    },
  ],
}
