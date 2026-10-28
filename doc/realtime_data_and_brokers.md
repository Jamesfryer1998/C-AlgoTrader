# Real-time data and brokers: research notes (October 2026)

Goal: replace "Python downloads Yahoo data every minute → C++ reads a CSV → IBKR desktop app sends the order"
with one C++ process that receives market data the moment it is published and sends orders to a broker
over a web API. It should be free, or cost very little.

## Recommendation

**Use Alpaca for both the data and the orders.** One account and one set of API keys gives you:

- a **free real-time WebSocket** of 1-minute bars, trades and quotes (IEX exchange feed)
- a **REST order API** with a free **paper trading** account (fake $100k), open to UK residents
- the same code for live trading later: you only change `trading_url`
- nothing to keep running on your computer apart from our program (no TWS / Gateway)

The new `src/alpaca/` code uses exactly this. If the free IEX data turns out not to be good enough,
the only upgrade is Alpaca's *Algo Trader Plus* plan ($99/month) and changing `"data_feed": "iex"` to `"sip"`.

## Market data options

| Provider | Price | Real-time? | How it arrives | Notes |
|---|---|---|---|---|
| **Alpaca Basic** | **Free** | Yes, IEX exchange only | WebSocket (bars, trades, quotes), 30 symbols | 200 REST calls/min. IEX handles roughly 4% of US share volume, so bar *prices* track the market closely but *volume* is much lower than the full market, and a thinly traded stock can have minutes with no IEX trades (no bar that minute). NVDA trades on IEX constantly. |
| Alpaca Algo Trader Plus | $99/month | Yes, all US exchanges (SIP) | WebSocket, unlimited symbols | 10,000 REST calls/min. The "proper" consolidated feed. |
| Finnhub free | Free | Yes, US trades | WebSocket (trades only) | ~60 REST calls/min, personal use only. You would build your own 1-minute bars from trades. The next plan up is very expensive, so there's no cheap upgrade path. No order API. |
| Massive (formerly Polygon.io) | $0 / $29 / $79 / **$199** per month | Only the $199 Advanced plan | WebSocket | Free = end of day. $29 and $79 plans are 15-minute delayed. Excellent data but too expensive for real-time. No order API. |
| Tiingo | Free / $30 per month | IEX real-time | REST + WebSocket | Free tier is 50 requests/hour. It wasn't clear which tier includes the WebSocket. No order API. |
| Databento | $199/month (Standard) | Yes | Native C++ client | Professional grade (exchange-direct, CME futures, order book). Overkill and over budget for now. |
| IG (streaming API) | Free with account | Yes | Lightstreamer | **"Subscription to equity prices is not available"** through IG's API, so no NVDA stream. Fine for indices, FX and commodities (e.g. the EnergyPrediction gas CFDs). 40 subscriptions max. |
| Yahoo Finance (what we used) | Free | No (unofficial, delayed, polled) | Python scraping | No official API, no streaming, rate-limited without warning. |

## Brokers with a web API (no desktop app)

| Broker | API | UK resident? | Paper/demo | Good fit for this project? |
|---|---|---|---|---|
| **Alpaca** | REST + WebSocket | Yes (live account needs W-8BEN tax form) | Yes, free, sign-up only | **Yes.** US stocks/ETFs/options, commission-free. Data and orders in one place. |
| IG | REST + Lightstreamer | Yes | Yes | Good for spread bets/CFDs on indices, FX and commodities. No equity price streaming, and 100 trading requests/min. Not a fit for NVDA. |
| Trading 212 | REST (Basic auth) | Yes | Yes (demo) | Invest/ISA accounts only, no market data stream, so you'd still need a separate data feed. |
| Saxo OpenAPI | REST + WebSocket | Yes | Yes (SIM, 24h developer tokens) | Powerful, but live access needs an approved app and an OAuth flow. More setup than Alpaca. |
| IBKR Web API | REST + WebSocket | Yes | Yes | The direct OAuth route (no gateway) is offered to advisors, organisations and third-party vendors. Retail accounts still run the Client Portal Gateway (a small Java process), which is the same "something must be running" problem. |

## Speed: where the time goes

Previously the decision could lag the market by up to a minute (the 60s sleep), plus the time for Python to
download a week of data from Yahoo, write a CSV and for C++ to re-read it.

Now each step is:

| Step | Typical time | Notes |
|---|---|---|
| Minute closes → Alpaca pushes the bar | Printed on every bar as `arrived Xs after the minute closed` | Measure it on your connection rather than trusting a number from a website. |
| Strategy decision (RSI etc.) | ~0.2 ms | Measured locally. Printed as `Decision took ...`. |
| Order to Alpaca (REST) | Roughly 100–300 ms from a UK home connection (estimate) | Mostly the UK→US network round trip. Included in `Decision took ...` when an order is sent. |

So the system now reacts within about a second of each minute closing instead of up to a minute later.

If you want it faster still, in order of effort:
1. Subscribe to `trades` or `quotes` instead of `bars` and react tick-by-tick (same WebSocket, one line change, but strategies would need to work on ticks).
2. Run the program on a small server in the US East region, close to Alpaca, to cut the order round trip to a few milliseconds.
3. For true low latency you'd move to exchange-direct data (Databento) and a broker with FIX access, which is well beyond "free".

## Things to be aware of

- **Pattern Day Trader rule (live only):** Alpaca enforces the US PDT rule. A margin account under $25k can make at most 3 day trades in 5 business days. The paper account starts at $100k, so it's not affected.
- **Bracket orders:** when a strategy sets a stop loss and take profit, we send a bracket order so Alpaca manages the exits on its servers. While those exit orders are open, the shares are "held" by them, so a separate SELL for the same shares will be rejected until the bracket is closed.
- **Market hours:** IEX bars only arrive while the US market trades (14:30–21:00 UK time for most of the year, plus some pre/post-market). Use `"data_feed": "test"` to see bars at any time.
- **One stream per account:** Alpaca allows one market data WebSocket per account. Running two copies of the program gives `406 connection limit exceeded`.

## Sources

- [Alpaca: About Market Data API (plans and limits)](https://docs.alpaca.markets/docs/about-market-data-api)
- [Alpaca: WebSocket stream (URLs, auth, bar format, test stream)](https://docs.alpaca.markets/us/docs/streaming-market-data)
- [Alpaca: live trading account as a non-US resident](https://alpaca.markets/learn/live-trading-account-non-us/)
- [Rosenblatt: US securities volumes, May 2026 (IEX share 3.97%)](https://www.rblt.com/market-structure-reports/us-securities-volumes-may-2026)
- [Finnhub pricing summary](https://apicostcalc.com/finnhub.html) and [Finnhub WebSocket write-up](https://orthogonal.info/free-real-time-stock-alert-system-finnhub-websockets/)
- [Massive (Polygon) pricing](https://api.qveris.ai/guides/polygon-pricing-optimized) and [Massive WebSocket docs](https://massive.com/docs/websocket/stocks/trades.md)
- [Tiingo pricing comparison](https://www.findmymoat.com/vs/databento-vs-tiingo)
- [Databento pricing](https://databento.com/pricing)
- [IG Labs API FAQ (limits, no equity streaming)](https://net-labs.ig.com/faq.html)
- [Trading 212 API docs](https://docs.trading212.com)
- [Saxo OpenAPI environments](https://developer.saxo/openapi/learn/environments)
- [IBKR Web API OAuth 1.0a](https://www.interactivebrokers.com/docs/web-api/authentication/oauth-1a/introduction.md)
