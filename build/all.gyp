{
  'includes': [
    'common.gypi',
  ],
  'targets': [
    {
      'target_name': 'All',
      'type': 'none',
      'dependencies': [
          '<(DEPTH)/src/llvm/tests/tests.gyp:*',
          '<(DEPTH)/src/llvm/tf/tests/tests.gyp:*',
      ]
    },
  ],
}
