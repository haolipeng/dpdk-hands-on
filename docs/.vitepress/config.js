import { defineConfig } from 'vitepress'

export default defineConfig({
  title: 'DPDK Hands-on',
  description: 'DPDK 入门实战教程',
  lang: 'zh-CN',
  base: '/dpdk-hands-on/',

  // 忽略源代码文件链接（这些文件在 GitHub 仓库中，不在 docs 目录）
  ignoreDeadLinks: [
    /\.\/\d+-\w+\/.*\.c$/,
    /\.\/\d+-\w+\/.*\.h$/,
    /\.\/DPDK-Ring-HTS-Guide$/,
    /\.\/lesson\d+/,
    /\.\/.*\.c$/,
  ],

  head: [
    ['link', { rel: 'icon', href: '/dpdk-hands-on/favicon.ico' }]
  ],

  themeConfig: {
    logo: '/logo.png',

    nav: [
      { text: '首页', link: '/' },
      { text: '教程', link: '/lessons/lesson1-helloworld' },
      { text: '���题指南', link: '/guides/ring-practical' },
      {
        text: '参考资源',
        items: [
          { text: 'DPDK 官网', link: 'https://www.dpdk.org/' },
          { text: 'DPDK 文档', link: 'https://doc.dpdk.org/' },
          { text: 'DPDK API', link: 'https://doc.dpdk.org/api/' }
        ]
      }
    ],

    sidebar: {
      '/lessons/': [
        {
          text: '基础入门',
          collapsed: false,
          items: [
            { text: 'Lesson 1: Hello World', link: '/lessons/lesson1-helloworld' },
            { text: 'Lesson 2: Hash 哈希表', link: '/lessons/lesson2-hash' }
          ]
        },
        {
          text: '网络数据包处理',
          collapsed: false,
          items: [
            { text: 'Lesson 3: 数据包捕获', link: '/lessons/lesson3-capture-packet' },
            { text: 'Lesson 3+: 捕获优化', link: '/lessons/lesson3-capture-packet-improve' },
            { text: 'Lesson 4: 数据包解析', link: '/lessons/lesson4-parse-packet' }
          ]
        },
        {
          text: '内存管理',
          collapsed: false,
          items: [
            { text: 'Lesson 5: Mempool 内存池', link: '/lessons/lesson5-mempool' },
            { text: 'Lesson 13: Mbuf 入门', link: '/lessons/lesson13-mbuf-beginner' }
          ]
        },
        {
          text: '流管理',
          collapsed: false,
          items: [
            { text: 'Lesson 6: Flow Manager', link: '/lessons/lesson6-flowmanager' }
          ]
        },
        {
          text: '多进程架构',
          collapsed: false,
          items: [
            { text: 'Lesson 7: 基础多进程', link: '/lessons/lesson7-simple_mp' },
            { text: 'Lesson 7: Client/Server', link: '/lessons/lesson7-client_server_mp' },
            { text: 'Lesson 8: 进程间通信', link: '/lessons/lesson8-mp-ipc' }
          ]
        },
        {
          text: '定时器',
          collapsed: false,
          items: [
            { text: 'Lesson 9: Timer 定时器', link: '/lessons/lesson9-timer' }
          ]
        },
        {
          text: 'Ring 队列',
          collapsed: false,
          items: [
            { text: 'Lesson 10: SP/SC & MP/MC', link: '/lessons/lesson10-ring-spsc-mpmc' },
            { text: 'Lesson 12: HTS Ring', link: '/lessons/lesson12-hts-ring' }
          ]
        },
        {
          text: '其他',
          collapsed: false,
          items: [
            { text: '时间周期与性能测试', link: '/lessons/lesson-time-cycles-benchmark' }
          ]
        }
      ],
      '/guides/': [
        {
          text: '专题指南',
          items: [
            { text: 'Ring 实战指南', link: '/guides/ring-practical' },
            { text: 'Ring HTS 模式', link: '/guides/ring-hts' },
            { text: 'Ring RTS 零拷贝', link: '/guides/ring-rts-zerocopy' },
            { text: 'Mbuf 实战指南', link: '/guides/mbuf-practical' }
          ]
        }
      ]
    },

    socialLinks: [
      { icon: 'github', link: 'https://github.com/haolipeng/dpdk-hands-on' }
    ],

    footer: {
      message: 'Released under the MIT License.',
      copyright: 'Copyright © 2024 DPDK Hands-on'
    },

    search: {
      provider: 'local',
      options: {
        translations: {
          button: {
            buttonText: '搜索文档',
            buttonAriaLabel: '搜索文档'
          },
          modal: {
            noResultsText: '无法找到相关结果',
            resetButtonTitle: '清除查询条件',
            footer: {
              selectText: '选择',
              navigateText: '切换'
            }
          }
        }
      }
    },

    outline: {
      label: '本页目录',
      level: [2, 3]
    },

    lastUpdated: {
      text: '最后更新于',
      formatOptions: {
        dateStyle: 'short',
        timeStyle: 'short'
      }
    },

    docFooter: {
      prev: '上一篇',
      next: '下一篇'
    },

    darkModeSwitchLabel: '主题',
    sidebarMenuLabel: '菜单',
    returnToTopLabel: '回到顶部'
  }
})
