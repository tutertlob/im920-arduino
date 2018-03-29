# im920-arduino
Arduino library for communicating among IM920 wireless modules from Interplan.

通信モジュールIM920を使用するためのArduinoライブラリ。Java用ライブラリは[こちら](https://github.com/tutertlob/im920-java)。

IM920でデータ通信するため本ライブラリでは４つのパケットタイプを定義し、IM920のフレームに乗せて送信する。

## IM920 frame
下記はIM920の受信データ形式。IM920は最大で64バイトのデータを送信可能。本ライブラリの４つのタイプのパケットはBody部分に格納され送信される。

<table>
  <tr>
    <th colspan="3">Header</th>
    <th>Body</th>
  </tr>
  <tr>
    <td>Octets: 2</td>
    <td>4</td>
    <td>2</td>
    <td>1 to 64</td>
  </tr>
  <tr>
    <td>Node Id</td>
    <td>Module Id</td>
    <td>RSSI</td>
    <td>User data</td>
  </tr>
</table>

## IM920 packet format
下記は本ライブラリで定義するパケットの構造で、ヘッダーとペイロード部がある。IM920パケットはIM920フレームのBody部に格納される。

<table>
  <tr>
    <th colspan="7">Frame body</th>
  </tr>
  <tr>
    <th colspan="2">Octets: 1</th>
    <th colspan="3">1</th>
    <th>1</th>
    <th>1 to 61</th>
  </tr>
  <tr align="center">
    <td>Reserved</br>(2 bits)</td>
    <td>Frame length</br>(6 bits)</td>
    <td>Reserved</br>(3 bits)</td>
    <td>Flags</br>(2 bits)</td>
    <td>Packet types</br>(3 bits)</td>
    <td>Seq num</td>
    <td width="250rem">Payload</td>
  </tr>
</table>


* Packet length (6 bits)

  パケットのペイロード部に格納されているデータサイズ。有効長: 1〜61オクテット。

* Flags (2 bits)
    * Fragment (Bit: 4)
      </br>後続する分割されたDataパケットの有無を表す。</br>1: 分割されたデータパケットが後続に続くことを意味する。</br>0: データが分割されていないか、また 分割されたデータパケットのうち最終パケットであることを示す。
      
    * Response request (Bit: 3)
      </br>Commandパケットへの応答要求の有無を表す。</br>1: 応答要求あり</br>0: 応答要求なし

* Packet types (3 bits)
    * Data packet (000)
    * Command packet (001)
    * Ack packet (010)
    * Notice packet (011)
    * Reserved (100 - 111)


* Sequence number (8 bits)

  パケット送信時に付与され1ずつインクリメントされる。有効範囲は0-255。255に達するとカウンターが一巡し0に戻る。

* Payload

  パケットのペイロード。最大で61バイトまでのデータを格納可能。
  
### Data packet
Dataパケットはパケットペイロードを最大限使用し、一度に最大61バイトのバイナリ形式のデータを送信可能。送信データが61バイトを超える場合は、フラグメントフラグを併用することで61バイト以上のデータを複数のDataパケットに分割しながら送信可能となる。
<table>
  <tr>
    <th>Payload</th>
  </tr>
  <tr>
    <td>Octets: 1 to 61</td>
  </tr>
  <tr>
    <td>データ</td>
  </tr>
</table>

### Command pakcet
センサーなどリモート側にコマンドを送信し、遠隔操作するために使用することを想定している。

Commandパケットにおけるパケットペイロードはコマンド種別(1 octet)とコマンドパラメータ(1-60 octets)の２つ部分で構成される。パラメータは最大60バイトまででASCII形式の文字列で格納される。

<table>
  <tr>
    <th colspan="2">Payload</th>
  </tr>
  <tr>
    <td>Octets: 1</td>
    <td>1 to 60</td>
  </tr>
  <tr>
    <td>コマンド種別</td>
    <td>コマンドパラメータ</td>
  </tr>
</table>

コマンド種別には予め用途が定められてシステムで予約されているものがあるが、それ以外は任意に使用可能。例えば`0x00`と`0x01`はシステムで予約されており、`0x01`はリモート（センサー）側のIM920通信モジュールを遠隔設定するためのIM920制御コマンド送信に使用する。

| コマンド種別 | 説明  |
|:------------|:-----|
| 0x00 | システム予約コマンド |
| 0x01 | システム予約コマンド。</br>リモート（センサー）側のIM920通信モジュールを遠隔設定するためのIM920制御コマンド送信に使用する。|
| 0x02-0xFF | ユーザー開放コマンド。自由にコマンドを定義可能。 |

リモート側でIM920の送信出力を読み出すコマンドを実行する場合:
```
コマンド種別: 0x01, コマンドパラメータ: RDPO
```

### Ack packet
リモート側で受信したCommandパケットのコマンドを実行し、その実行結果をコマンド応答として送信をCommandパケット送信元がCommandパケットの`Response requestフラグ`で要求している場合に、それを送信するために使用することを想定している。

Ackパケットにおけるパケットペイロードはコマンド種別(1 octet)とコマンド応答(1-60 octets)の２つ部分で構成される。コマンド応答は最大60バイトまででASCII形式の文字列で格納される。Ackパケットのコマンド種別は要求元のCommandパケットのコマンド種別と一致させる必要がある。これによりAckパケットのコマンド応答がどのコマンド要求に対するものかが要求元で判別可能となる。

<table>
  <tr>
    <th colspan="2">Payload</th>
  </tr>
  <tr>
    <td>Octets: 1</td>
    <td>1 to 60</td>
  </tr>
  <tr>
    <td>コマンド種別</td>
    <td>コマンド応答</td>
  </tr>
</table>

### Notice packet
任意の用途に使用可能で、任意の通知文、ASCII形式の文字列データ(1-61バイト)を送信することが可能。
<table>
  <tr>
    <th>Payload</th>
  </tr>
  <tr>
    <td>Octets: 1 to 61</td>
  </tr>
  <tr>
    <td>通知文</td>
  </tr>
</table>



## Configuring IM920 wireless module
IM920通信モジュールを設定する。Interplanから出ているUSB interface boardを使用し、PCと接続して行う。
以下の項目を設定する。設定コマンドはInterplanのマニュアルを参照。送受信側双方同じ設定にする。
* ボーレート: 38,400 bps（初期値: 19,200 bps）
* 動作モード: データモード（初期値: データモード）
* キャラクタ入出力: DCIO（初期値: DCIO)
* 受信ID登録(初期値: 未登録)
  </br>送信側（相手）モジュールのIDを自身に登録する。双方向通信する場合はお互いのモジュールIDを登録し合う。
  
