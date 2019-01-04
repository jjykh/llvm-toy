{
    'variables': {
      'clang%': 1,
      'conditions': [
        ['OS == "linux"',
            {
                'shared_library_cflags': [
                    '-fPIC',
                ]
            }
        ],
        ['OS == "win"',
            {
                'shared_library_cflags': [
                ],
            }
        ],
      ],
  },
  'conditions': [
      ['clang == 1', {
          'make_global_settings': [
              ['CXX','/usr/bin/clang++'],
              ['LINK','/usr/bin/clang++'],
          ],
      }, {}
      ],
  ],
  'target_defaults': {
      'default_configuration': 'Debug',
      'configurations': {
          'Common_Base': {
              'abstract': 1,
              'include_dirs': [
                  '<(DEPTH)',
                  '<(DEPTH)/v8_fakeincludes',
              ],
          },
          'Debug_Base': {
              'abstract': 1,
              'defines': [
                  'LOG_LEVEL=5',
              ],
              'conditions': [
                ['OS == "linux"',
                  {
                      'cflags': [
                        '-O0',
                        '-g3',
                      ],
                      'cflags_cc': [
                        '-std=c++14',
                      ],
                  }
                ],
              ],
           },
           'Debug':  {
               'inherit_from': ['Common_Base', 'Debug_Base'],
           },
           'Release_Base': {
              'abstract': 1,
              'defines': [
                  'NDEBUG',
              ],
              'conditions': [
                ['OS == "linux"',
                  {
                      'cflags': [
                        '-O2',
                        '-g'
                      ],
                      'cflags_cc': [
                        '-std=c++14',
                      ],
                  }
                ],
              ],
           },
           'Release':  {
               'inherit_from': ['Common_Base', 'Release_Base'],
           },
      },
  },
}
