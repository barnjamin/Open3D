import numpy as np
import matplotlib.pyplot as plt


def load_losses_log(log_path):
    with open(log_path, 'r') as fin:
        lines = fin.read().splitlines()

        # We assume fixed 3 level pyramids
        losses_0 = []
        losses_1 = []
        losses_2 = []
        for i in range(0, len(lines) - 4, 4):
            loss_0 = [float(str_number) for str_number in lines[i + 1].split(' ')[:-1]]
            if len(loss_0) == 60:
                losses_0.append(loss_0)

            loss_1 = [float(str_number) for str_number in lines[i + 2].split(' ')[:-1]]
            if len(loss_1) == 60:
                losses_1.append(loss_1)

            loss_2 = [float(str_number) for str_number in lines[i + 3].split(' ')[:-1]]
            if len(loss_2) == 60:
                losses_2.append(loss_2)

        return np.stack(losses_0), np.stack(losses_1), np.stack(losses_2)


if __name__ == '__main__':
    plt.rc('text', usetex=True)
    plt.rc('font', family='serif')

    losses_0, losses_1, losses_2 = load_losses_log(
        '../../cmake-build-release/bin/examples/odometry_less_assoc_step_1.log')

    fig, ax = plt.subplots(1, 1)

    nframes, niters = losses_0.shape
    for i in range(0, nframes, 60):
        ax.plot(np.arange(niters), np.log(losses_0[i, :]), linewidth=1)

    ax.grid()
    ax.set_xlabel(r'\textbf{Iterations}')
    ax.set_xticks(np.arange(0, niters, 4))
    ax.set_ylabel(r'Log average \textit{loss}', fontsize=16)
    ax.set_title(r'$\lambda=0.5$, step=1, insufficient data association',
                 fontsize=16) # ,
    # color='gray')

    # Make room for the ridiculously large title.
    plt.subplots_adjust(top=0.8)
    plt.savefig('fc.png')
    plt.show()
